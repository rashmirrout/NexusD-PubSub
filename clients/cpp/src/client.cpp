/**
 * @file client.cpp
 * @brief NexusD C++ Client Implementation
 */

#include "nexusd_client/client.hpp"

#include <grpcpp/grpcpp.h>
#include "sidecar.grpc.pb.h"

#include <atomic>
#include <stdexcept>

namespace nexusd {

/**
 * @brief Client implementation (PIMPL)
 */
class ClientImpl {
public:
    explicit ClientImpl(const std::string& address)
        : address_(address)
        , connected_(false)
    {
        connect();
    }
    
    ~ClientImpl() = default;
    
    void connect() {
        channel_ = grpc::CreateChannel(address_, grpc::InsecureChannelCredentials());
        stub_ = nexusd::sidecar::SidecarService::NewStub(channel_);
        connected_ = true;
    }
    
    PublishResult publish(
        const std::string& topic,
        const std::vector<uint8_t>& payload,
        const PublishOptions& options
    ) {
        nexusd::sidecar::PublishRequest request;
        request.set_topic(topic);
        request.set_payload(payload.data(), payload.size());
        request.set_content_type(options.content_type);
        request.set_retain(options.retain);
        request.set_correlation_id(options.correlation_id);
        request.set_ttl_ms(options.ttl_ms);
        
        nexusd::sidecar::PublishResponse response;
        grpc::ClientContext context;
        
        grpc::Status status = stub_->Publish(&context, request, &response);
        
        PublishResult result;
        if (status.ok()) {
            result.success = response.success();
            result.message_id = response.message_id();
            result.subscriber_count = response.subscriber_count();
            result.error_message = response.error_message();
        } else {
            result.success = false;
            result.error_message = status.error_message();
        }
        
        return result;
    }
    
    void subscribe(
        const std::vector<std::string>& topics,
        MessageCallback callback,
        ErrorCallback error_callback,
        const SubscribeOptions& options
    ) {
        nexusd::sidecar::SubscribeRequest request;
        for (const auto& topic : topics) {
            request.add_topics(topic);
        }
        request.set_client_id(options.client_id);
        request.set_receive_retained(options.receive_retained);
        request.set_max_buffer_size(options.max_buffer_size);
        
        grpc::ClientContext context;
        auto reader = stub_->Subscribe(&context, request);
        
        nexusd::sidecar::MessageEvent event;
        while (reader->Read(&event)) {
            Message msg;
            
            if (event.has_message()) {
                const auto& m = event.message();
                msg.message_id = m.message_id();
                msg.topic = m.topic();
                msg.payload = std::vector<uint8_t>(m.payload().begin(), m.payload().end());
                msg.content_type = m.content_type();
                msg.timestamp_ms = m.timestamp_ms();
                msg.source_node_id = m.source_node_id();
                msg.correlation_id = m.correlation_id();
                msg.is_retained = false;
                callback(msg);
            } else if (event.has_retained_message()) {
                const auto& m = event.retained_message();
                msg.message_id = m.message_id();
                msg.topic = m.topic();
                msg.payload = std::vector<uint8_t>(m.payload().begin(), m.payload().end());
                msg.content_type = m.content_type();
                msg.timestamp_ms = m.timestamp_ms();
                msg.source_node_id = m.source_node_id();
                msg.correlation_id = m.correlation_id();
                msg.is_retained = true;
                callback(msg);
            }
            // Ignore subscription_info and heartbeat
        }
        
        grpc::Status status = reader->Finish();
        if (!status.ok() && status.error_code() != grpc::StatusCode::CANCELLED) {
            if (error_callback) {
                error_callback(status.error_message());
            }
        }
    }
    
    bool unsubscribe(const std::string& subscription_id) {
        nexusd::sidecar::UnsubscribeRequest request;
        request.set_subscription_id(subscription_id);
        
        nexusd::sidecar::UnsubscribeResponse response;
        grpc::ClientContext context;
        
        grpc::Status status = stub_->Unsubscribe(&context, request, &response);
        return status.ok() && response.success();
    }
    
    std::vector<TopicInfo> getTopics() {
        nexusd::sidecar::GetTopicsRequest request;
        nexusd::sidecar::GetTopicsResponse response;
        grpc::ClientContext context;
        
        grpc::Status status = stub_->GetTopics(&context, request, &response);
        
        std::vector<TopicInfo> topics;
        if (status.ok()) {
            for (const auto& t : response.topics()) {
                topics.push_back({
                    t.topic(),
                    t.subscriber_count(),
                    t.has_retained()
                });
            }
        }
        return topics;
    }
    
    std::vector<PeerInfo> getPeers() {
        nexusd::sidecar::GetPeersRequest request;
        nexusd::sidecar::GetPeersResponse response;
        grpc::ClientContext context;
        
        grpc::Status status = stub_->GetPeers(&context, request, &response);
        
        std::vector<PeerInfo> peers;
        if (status.ok()) {
            for (const auto& p : response.peers()) {
                peers.push_back({
                    p.node_id(),
                    p.address(),
                    p.cluster_id(),
                    p.is_healthy()
                });
            }
        }
        return peers;
    }
    
    bool isConnected() const {
        return connected_.load();
    }

private:
    std::string address_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<nexusd::sidecar::SidecarService::Stub> stub_;
    std::atomic<bool> connected_;
};


// ============================================================================
// Client Implementation
// ============================================================================

Client::Client(const std::string& address)
    : impl_(std::make_unique<ClientImpl>(address))
{
}

Client::~Client() = default;

Client::Client(Client&&) noexcept = default;
Client& Client::operator=(Client&&) noexcept = default;

PublishResult Client::publish(
    const std::string& topic,
    const std::vector<uint8_t>& payload,
    const PublishOptions& options
) {
    return impl_->publish(topic, payload, options);
}

PublishResult Client::publish(
    const std::string& topic,
    const std::string& payload,
    const PublishOptions& options
) {
    return impl_->publish(
        topic,
        std::vector<uint8_t>(payload.begin(), payload.end()),
        options
    );
}

void Client::subscribe(
    const std::vector<std::string>& topics,
    MessageCallback callback,
    const SubscribeOptions& options
) {
    impl_->subscribe(topics, callback, nullptr, options);
}

void Client::subscribe(
    const std::vector<std::string>& topics,
    MessageCallback message_callback,
    ErrorCallback error_callback,
    const SubscribeOptions& options
) {
    impl_->subscribe(topics, message_callback, error_callback, options);
}

bool Client::unsubscribe(const std::string& subscription_id) {
    return impl_->unsubscribe(subscription_id);
}

std::vector<TopicInfo> Client::getTopics() {
    return impl_->getTopics();
}

std::vector<PeerInfo> Client::getPeers() {
    return impl_->getPeers();
}

bool Client::isConnected() const {
    return impl_->isConnected();
}

} // namespace nexusd
