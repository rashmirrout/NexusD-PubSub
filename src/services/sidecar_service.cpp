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
#include "nexusd/core/subscriber_queue.hpp"

#include "nexusd/proto/mesh.pb.h"

#include <chrono>
#include <queue>

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
    std::unique_ptr<core::SubscriberQueue<sidecar::MessageEvent>> messageQueue;  // Backpressure-enabled queue
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
                    
                    // Enqueue via backpressure queue
                    if (stream_->messageQueue) {
                        stream_->messageQueue->enqueue(std::move(retEvent), 0);
                    }
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

        // Dequeue from backpressure queue
        std::lock_guard<std::mutex> lock(stream_->writeMutex);
        if (stream_->messageQueue && !stream_->messageQueue->empty()) {
            auto msg = stream_->messageQueue->dequeue();
            if (msg) {
                currentEvent_ = std::move(*msg);
                StartWrite(&currentEvent_);
                return;
            }
        }
        stream_->writing.store(false);
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

    /**
     * @brief Enqueue a message with backpressure handling.
     * @param event The message event to enqueue.
     * @param ttlMs Message TTL (0 = use queue default).
     * @return EnqueueResult indicating success, dropped, or blocked.
     */
    core::EnqueueResult enqueueMessage(sidecar::MessageEvent event, int64_t ttlMs = 0) {
        if (!stream_->active.load()) {
            return core::EnqueueResult::DROPPED_DISCONNECTED;
        }

        core::EnqueueResult result = core::EnqueueResult::ENQUEUED;
        {
            std::lock_guard<std::mutex> lock(stream_->writeMutex);
            if (stream_->messageQueue) {
                result = stream_->messageQueue->enqueue(std::move(event), ttlMs);
            }
        }

        // Start writing if not already
        if (result == core::EnqueueResult::ENQUEUED && !stream_->writing.exchange(true)) {
            std::lock_guard<std::mutex> lock(stream_->writeMutex);
            if (stream_->messageQueue && !stream_->messageQueue->empty()) {
                auto msg = stream_->messageQueue->dequeue();
                if (msg) {
                    currentEvent_ = std::move(*msg);
                    StartWrite(&currentEvent_);
                }
            } else {
                stream_->writing.store(false);
            }
        }
        return result;
    }

    /**
     * @brief Get queue statistics for this subscriber.
     */
    core::QueueStats getQueueStats() const {
        std::lock_guard<std::mutex> lock(stream_->writeMutex);
        if (stream_->messageQueue) {
            return stream_->messageQueue->getStats();
        }
        return {};
    }

private:
    SidecarServiceImpl* service_;
    std::shared_ptr<core::PeerRegistry> registry_;
    std::shared_ptr<SidecarServiceImpl::SubscriptionStream> stream_;
    sidecar::MessageEvent currentEvent_;
};

// =============================================================================
// Helper to parse backpressure policy string
// =============================================================================

static core::BackpressurePolicy parsePolicyString(const std::string& policy) {
    if (policy == "drop-newest") {
        return core::BackpressurePolicy::DROP_NEWEST;
    } else if (policy == "block") {
        return core::BackpressurePolicy::BLOCK;
    }
    return core::BackpressurePolicy::DROP_OLDEST;  // default
}

// =============================================================================
// SidecarServiceImpl
// =============================================================================

SidecarServiceImpl::SidecarServiceImpl(
    std::shared_ptr<core::PeerRegistry> registry,
    std::shared_ptr<MeshClient> meshClient,
    uint32_t messageBufferSize,
    size_t maxBufferMemory,
    int64_t pausedSubscriptionTtl,
    uint32_t subscriberQueueLimit,
    const std::string& backpressurePolicy,
    int64_t blockTimeoutMs,
    int64_t defaultMessageTtlMs,
    int64_t ttlReaperIntervalMs)
    : registry_(std::move(registry))
    , meshClient_(std::move(meshClient))
    , messageBuffer_(std::make_unique<core::TopicMessageBuffer>(
          messageBufferSize, maxBufferMemory))
    , pausedSubscriptionTtl_(pausedSubscriptionTtl)
    , subscriberQueueLimit_(subscriberQueueLimit)
    , backpressurePolicy_(parsePolicyString(backpressurePolicy))
    , blockTimeoutMs_(blockTimeoutMs)
    , defaultMessageTtlMs_(defaultMessageTtlMs)
    , ttlReaperIntervalMs_(ttlReaperIntervalMs)
{
    LOG_INFO("SidecarService", "Created sidecar service (buffer_size={}, max_memory={}, ttl={}ms)",
             messageBufferSize, maxBufferMemory, pausedSubscriptionTtl);
    LOG_INFO("SidecarService", "Backpressure config: queue_limit={}, policy={}, block_timeout={}ms, default_ttl={}ms",
             subscriberQueueLimit, backpressurePolicy, blockTimeoutMs, defaultMessageTtlMs);

    // Start TTL reaper thread if TTL is enabled
    if (defaultMessageTtlMs > 0 && ttlReaperIntervalMs > 0) {
        reaperRunning_.store(true);
        reaperThread_ = std::thread(&SidecarServiceImpl::ttlReaperLoop, this);
        LOG_INFO("SidecarService", "TTL reaper started (interval={}ms)", ttlReaperIntervalMs);
    }
}

SidecarServiceImpl::~SidecarServiceImpl() {
    // Stop TTL reaper thread
    if (reaperRunning_.exchange(false)) {
        if (reaperThread_.joinable()) {
            reaperThread_.join();
        }
    }

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
                   sidecar::PublishResponse* response,
                   int64_t defaultTtlMs)
        : service_(service)
        , registry_(std::move(registry))
        , meshClient_(std::move(meshClient))
    {
        // Generate message ID
        std::string messageId = utils::UUIDGenerator::generate();
        
        // Determine effective TTL (publisher TTL takes precedence)
        int64_t effectiveTtl = request->ttl_ms() > 0 ? request->ttl_ms() : defaultTtlMs;
        
        LOG_DEBUG("SidecarService", "Publish: topic={}, msg_id={}, retain={}, ttl={}ms",
                  request->topic(), messageId, request->retain(), effectiveTtl);

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
    
    return new PublishReactor(this, registry_, meshClient_, request, response, defaultMessageTtlMs_);
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

    // Create backpressure-enabled queue for this subscriber
    stream->messageQueue = std::make_unique<core::SubscriberQueue<sidecar::MessageEvent>>(
        subscriberQueueLimit_,
        backpressurePolicy_,
        blockTimeoutMs_,
        defaultMessageTtlMs_);

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
                       sidecar::UnsubscribeResponse* response,
                       int64_t defaultTtlMs)
    {
        const std::string& subId = request->subscription_id();
        bool pause = request->pause();
        
        LOG_INFO("SidecarService", "Unsubscribe: id={} pause={}", subId, pause);

        // Remove from registry (with optional pause for later resumption)
        bool found = registry->removeLocalSubscription(subId, pause, defaultTtlMs);
        
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
    
    return new UnsubscribeReactor(this, registry_, request, response, pausedSubscriptionTtl_);
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
// ResumeSubscribe
// =============================================================================

class ResumeSubscribeReactor : public grpc::ServerWriteReactor<sidecar::MessageEvent> {
public:
    ResumeSubscribeReactor(SidecarServiceImpl* service,
                           std::shared_ptr<core::PeerRegistry> registry,
                           core::TopicMessageBuffer* messageBuffer,
                           std::shared_ptr<SidecarServiceImpl::SubscriptionStream> stream,
                           const sidecar::ResumeSubscribeRequest* request)
        : service_(service)
        , registry_(std::move(registry))
        , messageBuffer_(messageBuffer)
        , stream_(std::move(stream))
        , gapRecoveryMode_(request->gap_recovery_mode())
    {
        stream_->reactor = this;

        // Look up paused subscription
        core::PausedSubscription pausedInfo;
        bool resumed = registry_->resumeSubscription(
            request->subscription_id(),
            [](const core::RetainedMessage&) {},  // Placeholder callback
            pausedInfo);

        // Build subscription info
        sidecar::MessageEvent infoEvent;
        auto* info = infoEvent.mutable_subscription_info();
        info->set_subscription_id(stream_->subscription_id);
        for (const auto& topic : stream_->topics) {
            info->add_topics(topic);
        }
        info->set_timestamp_ms(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());

        bool gapDetected = false;
        uint64_t totalMissed = 0;

        if (resumed && gapRecoveryMode_ != sidecar::GapRecoveryMode::NONE) {
            // Perform gap recovery
            for (const auto& topic : stream_->topics) {
                uint64_t lastSeq = request->last_sequence_number();
                if (lastSeq == 0) {
                    // Use saved sequence from paused subscription
                    auto seqIt = pausedInfo.topic_sequences.find(topic);
                    if (seqIt != pausedInfo.topic_sequences.end()) {
                        lastSeq = seqIt->second;
                    } else {
                        lastSeq = pausedInfo.last_delivered_sequence;
                    }
                }

                bool topicGap = false;
                uint64_t topicMissed = 0;

                if (gapRecoveryMode_ == sidecar::GapRecoveryMode::RETAINED_ONLY) {
                    // Just send retained message
                    auto retained = messageBuffer_->getRetainedMessage(topic);
                    if (retained) {
                        sidecar::MessageEvent retEvent;
                        auto* msg = retEvent.mutable_retained_message();
                        msg->set_message_id(std::to_string(retained->sequence_number));
                        msg->set_topic(retained->topic);
                        msg->set_payload(retained->payload);
                        msg->set_timestamp_ms(
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                retained->timestamp.time_since_epoch()).count());
                        pendingReplay_.push(std::move(retEvent));
                    }
                } else if (gapRecoveryMode_ == sidecar::GapRecoveryMode::REPLAY_BUFFER) {
                    // Replay buffered messages
                    auto messages = messageBuffer_->getMessagesForReplay(
                        topic, lastSeq, topicGap, topicMissed);

                    for (const auto& buffered : messages) {
                        sidecar::MessageEvent replayEvent;
                        auto* replay = replayEvent.mutable_replay_message();
                        replay->set_message_id(std::to_string(buffered.sequence_number));
                        replay->set_topic(buffered.topic);
                        replay->set_payload(buffered.payload);
                        replay->set_sequence_number(buffered.sequence_number);
                        replay->set_timestamp_ms(
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                buffered.timestamp.time_since_epoch()).count());
                        pendingReplay_.push(std::move(replayEvent));
                    }
                }

                if (topicGap) {
                    gapDetected = true;
                    totalMissed += topicMissed;
                }
            }

            info->set_gap_detected(gapDetected);
            info->set_missed_message_count(totalMissed);
            info->set_replay_started(!pendingReplay_.empty());
        }

        LOG_INFO("SidecarService", "ResumeSubscribe: id={}, gap={}, missed={}, replay={}",
                 stream_->subscription_id, gapDetected, totalMissed, pendingReplay_.size());

        // Send subscription info first
        currentEvent_ = std::move(infoEvent);
        StartWrite(&currentEvent_);
    }

    void OnWriteDone(bool ok) override {
        if (!ok || !stream_->active.load()) {
            Finish(grpc::Status::OK);
            return;
        }

        // First drain replay queue
        if (!pendingReplay_.empty()) {
            currentEvent_ = std::move(pendingReplay_.front());
            pendingReplay_.pop();
            StartWrite(&currentEvent_);
            return;
        }

        // Send replay complete if we were replaying
        if (!replayCompleteSent_ && gapRecoveryMode_ == sidecar::GapRecoveryMode::REPLAY_BUFFER) {
            replayCompleteSent_ = true;
            sidecar::MessageEvent completeEvent;
            auto* complete = completeEvent.mutable_replay_complete();
            complete->set_subscription_id(stream_->subscription_id);
            complete->set_timestamp_ms(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
            currentEvent_ = std::move(completeEvent);
            StartWrite(&currentEvent_);
            return;
        }

        // Check for pending live messages (from backpressure queue)
        std::lock_guard<std::mutex> lock(stream_->writeMutex);
        if (stream_->messageQueue && !stream_->messageQueue->empty()) {
            auto msg = stream_->messageQueue->dequeue();
            if (msg) {
                currentEvent_ = std::move(*msg);
                StartWrite(&currentEvent_);
                return;
            }
        }
        stream_->writing.store(false);
    }

    void OnCancel() override {
        LOG_INFO("SidecarService", "ResumeSubscribe cancelled: id={}",
                 stream_->subscription_id);
        stream_->active.store(false);
    }

    void OnDone() override {
        LOG_DEBUG("SidecarService", "ResumeSubscribe done: id={}",
                  stream_->subscription_id);
        delete this;
    }

    core::EnqueueResult enqueueMessage(sidecar::MessageEvent event, int64_t ttlMs = 0) {
        if (!stream_->active.load()) {
            return core::EnqueueResult::DROPPED_DISCONNECTED;
        }

        core::EnqueueResult result = core::EnqueueResult::ENQUEUED;
        {
            std::lock_guard<std::mutex> lock(stream_->writeMutex);
            if (stream_->messageQueue) {
                result = stream_->messageQueue->enqueue(std::move(event), ttlMs);
            }
        }

        // Start writing if not already
        if (result == core::EnqueueResult::ENQUEUED && !stream_->writing.exchange(true)) {
            std::lock_guard<std::mutex> lock(stream_->writeMutex);
            if (stream_->messageQueue && !stream_->messageQueue->empty()) {
                auto msg = stream_->messageQueue->dequeue();
                if (msg) {
                    currentEvent_ = std::move(*msg);
                    StartWrite(&currentEvent_);
                }
            } else {
                stream_->writing.store(false);
            }
        }
        return result;
    }

private:
    SidecarServiceImpl* service_;
    std::shared_ptr<core::PeerRegistry> registry_;
    core::TopicMessageBuffer* messageBuffer_;
    std::shared_ptr<SidecarServiceImpl::SubscriptionStream> stream_;
    sidecar::GapRecoveryMode gapRecoveryMode_;
    sidecar::MessageEvent currentEvent_;
    std::queue<sidecar::MessageEvent> pendingReplay_;
    bool replayCompleteSent_ = false;
};

grpc::ServerWriteReactor<sidecar::MessageEvent>* SidecarServiceImpl::ResumeSubscribe(
    grpc::CallbackServerContext* context,
    const sidecar::ResumeSubscribeRequest* request) {
    
    // Get paused subscription info
    auto paused = registry_->getPausedSubscription(request->subscription_id());
    if (!paused) {
        // Return error via immediate finish
        class ErrorReactor : public grpc::ServerWriteReactor<sidecar::MessageEvent> {
        public:
            ErrorReactor() {
                Finish(grpc::Status(grpc::StatusCode::NOT_FOUND, 
                                   "No paused subscription found"));
            }
            void OnDone() override { delete this; }
        };
        return new ErrorReactor();
    }

    auto stream = std::make_shared<SubscriptionStream>();
    stream->subscription_id = request->subscription_id();
    stream->topics = paused->topics;

    // Create backpressure-enabled queue for this subscriber
    stream->messageQueue = std::make_unique<core::SubscriberQueue<sidecar::MessageEvent>>(
        subscriberQueueLimit_,
        backpressurePolicy_,
        blockTimeoutMs_,
        defaultMessageTtlMs_);

    // Track active stream
    {
        std::lock_guard<std::mutex> lock(streamsMutex_);
        activeStreams_[stream->subscription_id] = stream;
    }

    return new ResumeSubscribeReactor(this, registry_, messageBuffer_.get(), 
                                       stream, request);
}

// =============================================================================
// TTL Reaper Loop
// =============================================================================

void SidecarServiceImpl::ttlReaperLoop() {
    while (reaperRunning_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ttlReaperIntervalMs_));
        
        if (!reaperRunning_.load()) break;
        
        size_t totalReaped = 0;
        {
            std::lock_guard<std::mutex> lock(streamsMutex_);
            for (auto& [id, stream] : activeStreams_) {
                if (stream->messageQueue) {
                    totalReaped += stream->messageQueue->reapExpired();
                }
            }
        }
        
        if (totalReaped > 0) {
            LOG_DEBUG("SidecarService", "TTL reaper expired {} messages", totalReaped);
        }
    }
}

// =============================================================================
// deliverFromRemote
// =============================================================================

void SidecarServiceImpl::deliverFromRemote(const mesh::MessageEnvelope& envelope) {
    const std::string& topic = envelope.topic();
    
    // Buffer the message for gap recovery
    uint64_t sequenceNumber = 0;
    if (messageBuffer_) {
        sequenceNumber = messageBuffer_->bufferMessage(topic, envelope.payload());
    }
    
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
    msg->set_sequence_number(sequenceNumber);

    // Track delivery stats
    size_t deliveredCount = 0;
    size_t droppedCount = 0;
    size_t blockedCount = 0;

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

    // Use message TTL or default
    int64_t effectiveTtl = envelope.ttl_ms() > 0 ? envelope.ttl_ms() : defaultMessageTtlMs_;

    // Deliver to each matching stream with backpressure handling
    for (auto& stream : matchingStreams) {
        if (stream->reactor) {
            // Clone event for each subscriber
            sidecar::MessageEvent eventCopy = event;
            auto result = static_cast<SubscribeReactor*>(stream->reactor)
                             ->enqueueMessage(std::move(eventCopy), effectiveTtl);
            
            switch (result) {
                case core::EnqueueResult::ENQUEUED:
                    ++deliveredCount;
                    // Update sequence in registry
                    registry_->updateSubscriptionSequence(stream->subscription_id, topic, sequenceNumber);
                    break;
                case core::EnqueueResult::DROPPED_OLDEST:
                case core::EnqueueResult::DROPPED_NEWEST:
                case core::EnqueueResult::DROPPED_TTL:
                case core::EnqueueResult::DROPPED_DISCONNECTED:
                    ++droppedCount;
                    LOG_WARN("SidecarService", "Message dropped for subscriber {}: reason={}",
                             stream->subscription_id, static_cast<int>(result));
                    break;
                case core::EnqueueResult::BLOCKED:
                    ++blockedCount;
                    LOG_WARN("SidecarService", "Message blocked for subscriber {}, will retry",
                             stream->subscription_id);
                    break;
            }
        }
    }

    if (droppedCount > 0 || blockedCount > 0) {
        LOG_DEBUG("SidecarService", "Delivery stats for msg {}: delivered={}, dropped={}, blocked={}",
                  envelope.message_id(), deliveredCount, droppedCount, blockedCount);
    }

    LOG_TRACE("SidecarService", "Delivered message {} seq={} to {} streams",
              envelope.message_id(), sequenceNumber, matchingStreams.size());
}

}  // namespace services
}  // namespace nexusd
