/**
 * @file sidecar_service.cpp
 * @brief SidecarServiceImpl implementation.
 *
 * @copyright Copyright (c) 2024 NexusD Contributors
 * @license MIT License
 */

#include "nexusd/services/sidecar_service.hpp"
#include "nexusd/utils/logger.hpp"
#include "nexusd/utils/uuid.hpp"

#include "nexusd/proto/mesh.pb.h"

#include <chrono>

namespace nexusd {
namespace services {

// =============================================================================
// SubscriptionStream - holds state for an active Subscribe stream
// =============================================================================

struct SidecarServiceImpl::SubscriptionStream {
    std::string subscription_id;
    std::vector<std::string> topics;
    grpc::ServerWriteReactor<sidecar::MessageEvent>* reactor{nullptr};
    std::atomic<bool> active{true};
    std::mutex writeMutex;
    std::queue<sidecar::MessageEvent> pendingMessages;
    std::atomic<bool> writing{false};
};

// =============================================================================
// Subscribe Reactor
// =============================================================================

class SubscribeReactor : public grpc::ServerWriteReactor<sidecar::MessageEvent> {
public:
    SubscribeReactor(SidecarServiceImpl* service,
                     std::shared_ptr<core::PeerRegistry> registry,
                     std::shared_ptr<SidecarServiceImpl::SubscriptionStream> stream,
                     const sidecar::SubscribeRequest* request)
        : service_(service)
        , registry_(std::move(registry))
        , stream_(std::move(stream))
    {
        stream_->reactor = this;

        // Send subscription info as first message
        sidecar::MessageEvent event;
        auto* info = event.mutable_subscription_info();
        info->set_subscription_id(stream_->subscription_id);
        for (const auto& topic : stream_->topics) {
            info->add_topics(topic);
        }
        info->set_timestamp_ms(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());

        LOG_INFO("SidecarService", "Subscribe: id={}, topics={}",
                 stream_->subscription_id, stream_->topics.size());

        // Send retained messages if requested
        if (request->receive_retained()) {
            for (const auto& topic : stream_->topics) {
                auto retained = registry_->getRetainedMessage(topic);
                if (retained) {
                    sidecar::MessageEvent retEvent;
                    auto* msg = retEvent.mutable_retained_message();
                    msg->set_message_id(retained->message_id);
                    msg->set_topic(retained->topic);
                    msg->set_payload(retained->payload.data(), retained->payload.size());
                    msg->set_content_type(retained->content_type);
                    msg->set_timestamp_ms(retained->timestamp_ms);
                    msg->set_source_node_id(retained->source_node_id);
                    
                    std::lock_guard<std::mutex> lock(stream_->writeMutex);
                    stream_->pendingMessages.push(std::move(retEvent));
                }
            }
        }

        // Start writing
        currentEvent_ = std::move(event);
        StartWrite(&currentEvent_);
    }

    void OnWriteDone(bool ok) override {
        if (!ok || !stream_->active.load()) {
            Finish(grpc::Status::OK);
            return;
        }

        // Check for pending messages
        std::lock_guard<std::mutex> lock(stream_->writeMutex);
        if (!stream_->pendingMessages.empty()) {
            currentEvent_ = std::move(stream_->pendingMessages.front());
            stream_->pendingMessages.pop();
            StartWrite(&currentEvent_);
        } else {
            stream_->writing.store(false);
        }
    }

    void OnCancel() override {
        LOG_INFO("SidecarService", "Subscribe cancelled: id={}",
                 stream_->subscription_id);
        stream_->active.store(false);
    }

    void OnDone() override {
        LOG_DEBUG("SidecarService", "Subscribe done: id={}",
                  stream_->subscription_id);
        // Registry cleanup is handled elsewhere
        delete this;
    }

    void enqueueMessage(sidecar::MessageEvent event) {
        if (!stream_->active.load()) {
            return;
        }

        std::lock_guard<std::mutex> lock(stream_->writeMutex);
        stream_->pendingMessages.push(std::move(event));
        
        // Start writing if not already
        if (!stream_->writing.exchange(true)) {
            if (!stream_->pendingMessages.empty()) {
                currentEvent_ = std::move(stream_->pendingMessages.front());
                stream_->pendingMessages.pop();
                StartWrite(&currentEvent_);
            } else {
                stream_->writing.store(false);
            }
        }
    }

private:
    SidecarServiceImpl* service_;
    std::shared_ptr<core::PeerRegistry> registry_;
    std::shared_ptr<SidecarServiceImpl::SubscriptionStream> stream_;
    sidecar::MessageEvent currentEvent_;
};

// =============================================================================
// SidecarServiceImpl
// =============================================================================

SidecarServiceImpl::SidecarServiceImpl(
    std::shared_ptr<core::PeerRegistry> registry,
    std::shared_ptr<MeshClient> meshClient)
    : registry_(std::move(registry))
    , meshClient_(std::move(meshClient))
{
    LOG_INFO("SidecarService", "Created sidecar service");
}

SidecarServiceImpl::~SidecarServiceImpl() {
    // Cancel all active streams
    std::lock_guard<std::mutex> lock(streamsMutex_);
    for (auto& [id, stream] : activeStreams_) {
        stream->active.store(false);
    }
}

std::string SidecarServiceImpl::generateSubscriptionId() {
    uint64_t counter = subscriptionCounter_.fetch_add(1);
    return "sub-" + std::to_string(counter) + "-" + 
           utils::UUIDGenerator::generate().substr(0, 8);
}

// =============================================================================
// Publish
// =============================================================================

class PublishReactor : public grpc::ServerUnaryReactor {
public:
    PublishReactor(SidecarServiceImpl* service,
                   std::shared_ptr<core::PeerRegistry> registry,
                   std::shared_ptr<MeshClient> meshClient,
                   const sidecar::PublishRequest* request,
                   sidecar::PublishResponse* response)
        : service_(service)
        , registry_(std::move(registry))
        , meshClient_(std::move(meshClient))
    {
        // Generate message ID
        std::string messageId = utils::UUIDGenerator::generate();
        
        LOG_DEBUG("SidecarService", "Publish: topic={}, msg_id={}, retain={}",
                  request->topic(), messageId, request->retain());

        // Build envelope
        mesh::MessageEnvelope envelope;
        envelope.set_message_id(messageId);
        envelope.set_topic(request->topic());
        envelope.set_payload(request->payload());
        envelope.set_content_type(request->content_type());
        envelope.set_timestamp_ms(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        envelope.set_source_node_id(registry_->getLocalNodeId());
        envelope.set_retain(request->retain());
        envelope.set_correlation_id(request->correlation_id());
        envelope.set_ttl_ms(request->ttl_ms());

        // Store retained message if requested
        if (request->retain()) {
            core::RetainedMessage retained;
            retained.message_id = messageId;
            retained.topic = request->topic();
            retained.payload.assign(request->payload().begin(), request->payload().end());
            retained.content_type = request->content_type();
            retained.source_node_id = registry_->getLocalNodeId();
            retained.timestamp_ms = envelope.timestamp_ms();
            retained.ttl_ms = request->ttl_ms();
            registry_->setRetainedMessage(request->topic(), retained);
        }

        int subscriberCount = 0;

        // Forward to remote subscribers
        auto remoteNodes = registry_->getRemoteSubscribers(request->topic());
        for (const auto& nodeId : remoteNodes) {
            auto peer = registry_->getPeer(nodeId);
            if (peer) {
                // Async push
                meshClient_->pushMessage(peer->endpoint(), envelope,
                    [](bool success, const mesh::Ack& ack) {
                        if (!success) {
                            LOG_WARN("SidecarService", "Failed to push to remote");
                        }
                    });
                ++subscriberCount;
            }
        }

        // Deliver to local subscribers
        service_->deliverFromRemote(envelope);
        auto localSubs = registry_->getLocalSubscriptions(request->topic());
        subscriberCount += static_cast<int>(localSubs.size());

        // Build response
        response->set_success(true);
        response->set_message_id(messageId);
        response->set_subscriber_count(subscriberCount);

        if (subscriberCount == 0) {
            LOG_DEBUG("SidecarService", "No subscribers for topic {}", request->topic());
        }

        Finish(grpc::Status::OK);
    }

    void OnDone() override {
        delete this;
    }

private:
    SidecarServiceImpl* service_;
    std::shared_ptr<core::PeerRegistry> registry_;
    std::shared_ptr<MeshClient> meshClient_;
};

grpc::ServerUnaryReactor* SidecarServiceImpl::Publish(
    grpc::CallbackServerContext* context,
    const sidecar::PublishRequest* request,
    sidecar::PublishResponse* response) {
    
    return new PublishReactor(this, registry_, meshClient_, request, response);
}

// =============================================================================
// Subscribe
// =============================================================================

grpc::ServerWriteReactor<sidecar::MessageEvent>* SidecarServiceImpl::Subscribe(
    grpc::CallbackServerContext* context,
    const sidecar::SubscribeRequest* request) {
    
    auto stream = std::make_shared<SubscriptionStream>();
    stream->subscription_id = generateSubscriptionId();
    stream->topics.assign(request->topics().begin(), request->topics().end());

    // Register with peer registry
    registry_->addLocalSubscription(
        stream->subscription_id,
        request->client_id(),
        stream->topics,
        request->max_buffer_size(),
        [weak = std::weak_ptr<SubscriptionStream>(stream)](const core::RetainedMessage& msg) {
            // This callback is for future use with registry-based delivery
        });

    // Track active stream
    {
        std::lock_guard<std::mutex> lock(streamsMutex_);
        activeStreams_[stream->subscription_id] = stream;
    }

    return new SubscribeReactor(this, registry_, stream, request);
}

// =============================================================================
// Unsubscribe
// =============================================================================

class UnsubscribeReactor : public grpc::ServerUnaryReactor {
public:
    UnsubscribeReactor(SidecarServiceImpl* service,
                       std::shared_ptr<core::PeerRegistry> registry,
                       const sidecar::UnsubscribeRequest* request,
                       sidecar::UnsubscribeResponse* response)
    {
        const std::string& subId = request->subscription_id();
        
        LOG_INFO("SidecarService", "Unsubscribe: id={}", subId);

        // Remove from registry
        bool found = registry->removeLocalSubscription(subId);
        
        // Cancel the stream
        {
            std::lock_guard<std::mutex> lock(service->streamsMutex_);
            auto it = service->activeStreams_.find(subId);
            if (it != service->activeStreams_.end()) {
                it->second->active.store(false);
                service->activeStreams_.erase(it);
                found = true;
            }
        }

        response->set_success(found);
        if (!found) {
            response->set_error_message("Subscription not found");
        }

        Finish(grpc::Status::OK);
    }

    void OnDone() override {
        delete this;
    }
};

grpc::ServerUnaryReactor* SidecarServiceImpl::Unsubscribe(
    grpc::CallbackServerContext* context,
    const sidecar::UnsubscribeRequest* request,
    sidecar::UnsubscribeResponse* response) {
    
    return new UnsubscribeReactor(this, registry_, request, response);
}

// =============================================================================
// GetTopics
// =============================================================================

class GetTopicsReactor : public grpc::ServerUnaryReactor {
public:
    GetTopicsReactor(std::shared_ptr<core::PeerRegistry> registry,
                     const sidecar::GetTopicsRequest* request,
                     sidecar::GetTopicsResponse* response)
    {
        auto topics = registry->getLocalTopics();
        auto* topicMap = response->mutable_topics();
        
        for (const auto& topic : topics) {
            auto subs = registry->getLocalSubscriptions(topic);
            (*topicMap)[topic] = static_cast<int32_t>(subs.size());
        }

        Finish(grpc::Status::OK);
    }

    void OnDone() override {
        delete this;
    }
};

grpc::ServerUnaryReactor* SidecarServiceImpl::GetTopics(
    grpc::CallbackServerContext* context,
    const sidecar::GetTopicsRequest* request,
    sidecar::GetTopicsResponse* response) {
    
    return new GetTopicsReactor(registry_, request, response);
}

// =============================================================================
// GetPeers
// =============================================================================

class GetPeersReactor : public grpc::ServerUnaryReactor {
public:
    GetPeersReactor(std::shared_ptr<core::PeerRegistry> registry,
                    const sidecar::GetPeersRequest* request,
                    sidecar::GetPeersResponse* response)
    {
        auto peers = registry->getAllPeers();
        
        for (const auto& peer : peers) {
            auto* info = response->add_peers();
            info->set_instance_uuid(peer.instance_uuid);
            info->set_endpoint(peer.endpoint());
            info->set_last_seen_ms(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    peer.last_seen.time_since_epoch()).count());
            info->set_cluster_id(peer.cluster_id);
            info->set_topic_state_hash(peer.topic_state_hash);
            info->set_status(core::peerStatusToString(peer.status));
            
            if (request->include_state()) {
                for (const auto& topic : peer.topics) {
                    info->add_topics(topic);
                }
            }
        }

        Finish(grpc::Status::OK);
    }

    void OnDone() override {
        delete this;
    }
};

grpc::ServerUnaryReactor* SidecarServiceImpl::GetPeers(
    grpc::CallbackServerContext* context,
    const sidecar::GetPeersRequest* request,
    sidecar::GetPeersResponse* response) {
    
    return new GetPeersReactor(registry_, request, response);
}

// =============================================================================
// deliverFromRemote
// =============================================================================

void SidecarServiceImpl::deliverFromRemote(const mesh::MessageEnvelope& envelope) {
    const std::string& topic = envelope.topic();
    
    // Build MessageEvent
    sidecar::MessageEvent event;
    auto* msg = event.mutable_message();
    msg->set_message_id(envelope.message_id());
    msg->set_topic(topic);
    msg->set_payload(envelope.payload());
    msg->set_content_type(envelope.content_type());
    msg->set_timestamp_ms(envelope.timestamp_ms());
    msg->set_source_node_id(envelope.source_node_id());
    msg->set_correlation_id(envelope.correlation_id());

    // Find matching streams
    std::vector<std::shared_ptr<SubscriptionStream>> matchingStreams;
    {
        std::lock_guard<std::mutex> lock(streamsMutex_);
        for (auto& [id, stream] : activeStreams_) {
            if (!stream->active.load()) continue;
            
            for (const auto& subTopic : stream->topics) {
                if (subTopic == topic) {
                    matchingStreams.push_back(stream);
                    break;
                }
            }
        }
    }

    // Deliver to each matching stream
    for (auto& stream : matchingStreams) {
        if (stream->reactor) {
            static_cast<SubscribeReactor*>(stream->reactor)->enqueueMessage(event);
        }
    }

    LOG_TRACE("SidecarService", "Delivered message {} to {} streams",
              envelope.message_id(), matchingStreams.size());
}

}  // namespace services
}  // namespace nexusd
