/**
 * @file mesh_service.cpp
 * @brief MeshServiceImpl implementation.
 *
 * @copyright Copyright (c) 2024 NexusD Contributors
 * @license MIT License
 */

#include "nexusd/services/mesh_service.hpp"
#include "nexusd/services/sidecar_service.hpp"
#include "nexusd/utils/logger.hpp"

namespace nexusd {
namespace services {

MeshServiceImpl::MeshServiceImpl(std::shared_ptr<core::PeerRegistry> registry)
    : registry_(std::move(registry))
{
    LOG_INFO("MeshService", "Created mesh service");
}

void MeshServiceImpl::setSidecarService(SidecarService* sidecar) {
    sidecarService_ = sidecar;
}

// =============================================================================
// GetNodeState
// =============================================================================

class GetNodeStateReactor : public grpc::ServerUnaryReactor {
public:
    GetNodeStateReactor(std::shared_ptr<core::PeerRegistry> registry,
                        mesh::NodeState* response)
        : registry_(std::move(registry))
        , response_(response)
    {
        // Build response
        response_->set_instance_uuid(registry_->getLocalNodeId());
        
        auto topics = registry_->getLocalTopics();
        for (const auto& topic : topics) {
            response_->add_topics(topic);
        }
        
        response_->set_topic_state_hash(registry_->getLocalTopicHash());
        
        // Include retained messages
        auto retained = registry_->getAllRetainedMessages();
        for (const auto& [topic, msg] : retained) {
            auto* entry = response_->mutable_retained_messages();
            mesh::MessageEnvelope envelope;
            envelope.set_message_id(msg.message_id);
            envelope.set_topic(msg.topic);
            envelope.set_payload(msg.payload.data(), msg.payload.size());
            envelope.set_content_type(msg.content_type);
            envelope.set_timestamp_ms(msg.timestamp_ms);
            envelope.set_source_node_id(msg.source_node_id);
            envelope.set_retain(true);
            envelope.set_ttl_ms(msg.ttl_ms);
            (*entry)[topic] = envelope;
        }

        LOG_DEBUG("MeshService", "GetNodeState: returning {} topics", topics.size());
        Finish(grpc::Status::OK);
    }

    void OnDone() override {
        delete this;
    }

private:
    std::shared_ptr<core::PeerRegistry> registry_;
    mesh::NodeState* response_;
};

grpc::ServerUnaryReactor* MeshServiceImpl::GetNodeState(
    grpc::CallbackServerContext* context,
    const google::protobuf::Empty* request,
    mesh::NodeState* response) {
    
    return new GetNodeStateReactor(registry_, response);
}

// =============================================================================
// PushMessage
// =============================================================================

class PushMessageReactor : public grpc::ServerUnaryReactor {
public:
    PushMessageReactor(MeshServiceImpl* service,
                       const mesh::MessageEnvelope* request,
                       mesh::Ack* response)
        : service_(service)
        , request_(request)
        , response_(response)
    {
        LOG_DEBUG("MeshService", "PushMessage: topic={}, msg_id={}",
                  request_->topic(), request_->message_id());

        // Deliver to local subscribers
        service_->deliverLocally(*request_);

        response_->set_success(true);
        response_->set_message_id(request_->message_id());

        Finish(grpc::Status::OK);
    }

    void OnDone() override {
        delete this;
    }

private:
    MeshServiceImpl* service_;
    const mesh::MessageEnvelope* request_;
    mesh::Ack* response_;
};

grpc::ServerUnaryReactor* MeshServiceImpl::PushMessage(
    grpc::CallbackServerContext* context,
    const mesh::MessageEnvelope* request,
    mesh::Ack* response) {
    
    return new PushMessageReactor(this, request, response);
}

// =============================================================================
// PushMessageStream (bidirectional)
// =============================================================================

class PushMessageStreamReactor : public grpc::ServerBidiReactor<mesh::MessageEnvelope, mesh::Ack> {
public:
    PushMessageStreamReactor(MeshServiceImpl* service)
        : service_(service)
    {
        StartRead(&incoming_);
    }

    void OnReadDone(bool ok) override {
        if (ok) {
            LOG_DEBUG("MeshService", "Stream: received message for topic {}",
                      incoming_.topic());

            // Deliver locally
            service_->deliverLocally(incoming_);

            // Send ack
            ack_.set_success(true);
            ack_.set_message_id(incoming_.message_id());
            StartWrite(&ack_);
        } else {
            Finish(grpc::Status::OK);
        }
    }

    void OnWriteDone(bool ok) override {
        if (ok) {
            // Read next message
            StartRead(&incoming_);
        } else {
            Finish(grpc::Status::OK);
        }
    }

    void OnDone() override {
        delete this;
    }

private:
    MeshServiceImpl* service_;
    mesh::MessageEnvelope incoming_;
    mesh::Ack ack_;
};

grpc::ServerBidiReactor<mesh::MessageEnvelope, mesh::Ack>* 
MeshServiceImpl::PushMessageStream(grpc::CallbackServerContext* context) {
    return new PushMessageStreamReactor(this);
}

// =============================================================================
// Internal: Local delivery
// =============================================================================

void MeshServiceImpl::deliverLocally(const mesh::MessageEnvelope& envelope) {
    if (sidecarService_) {
        reinterpret_cast<SidecarServiceImpl*>(sidecarService_)->deliverFromRemote(envelope);
    }
}

}  // namespace services
}  // namespace nexusd
