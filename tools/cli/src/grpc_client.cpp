/**
 * @file grpc_client.cpp
 * @brief gRPC client implementation
 */

#include "grpc_client.hpp"

#include <grpcpp/grpcpp.h>
#include "sidecar.grpc.pb.h"
#include "mesh.grpc.pb.h"

namespace nexusd::cli {

struct GrpcClient::Impl {
    std::shared_ptr<grpc::Channel> channel;
    std::unique_ptr<nexusd::proto::Sidecar::Stub> sidecar_stub;
    std::unique_ptr<nexusd::proto::MeshService::Stub> mesh_stub;
    std::string address;
    bool connected = false;
};

GrpcClient::GrpcClient() : impl_(std::make_unique<Impl>()) {}

GrpcClient::~GrpcClient() {
    disconnect();
}

bool GrpcClient::connect(const std::string& address) {
    disconnect();
    
    impl_->channel = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
    
    // Wait for connection with timeout
    auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(5);
    if (!impl_->channel->WaitForConnected(deadline)) {
        impl_->channel.reset();
        return false;
    }
    
    impl_->sidecar_stub = nexusd::proto::Sidecar::NewStub(impl_->channel);
    impl_->mesh_stub = nexusd::proto::MeshService::NewStub(impl_->channel);
    impl_->address = address;
    impl_->connected = true;
    
    return true;
}

void GrpcClient::disconnect() {
    if (impl_) {
        impl_->sidecar_stub.reset();
        impl_->mesh_stub.reset();
        impl_->channel.reset();
        impl_->connected = false;
        impl_->address.clear();
    }
}

bool GrpcClient::is_connected() const {
    return impl_ && impl_->connected;
}

std::string GrpcClient::get_address() const {
    return impl_ ? impl_->address : "";
}

int64_t GrpcClient::ping() {
    if (!is_connected()) return -1;
    
    auto start = std::chrono::steady_clock::now();
    
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    
    nexusd::proto::PingRequest request;
    nexusd::proto::PingResponse response;
    
    auto status = impl_->sidecar_stub->Ping(&context, request, &response);
    
    if (!status.ok()) return -1;
    
    auto end = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

std::string GrpcClient::publish(const std::string& topic, const std::string& payload,
                                 const std::string& content_type, bool retain) {
    if (!is_connected()) return "";
    
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(10));
    
    nexusd::proto::PublishRequest request;
    request.set_topic(topic);
    request.set_payload(payload);
    request.set_content_type(content_type);
    request.set_retain(retain);
    
    nexusd::proto::PublishResponse response;
    auto status = impl_->sidecar_stub->Publish(&context, request, &response);
    
    if (!status.ok()) return "";
    
    return response.message_id();
}

std::string GrpcClient::subscribe(const std::string& pattern,
                                   std::function<void(const ReceivedMessage&)> callback) {
    if (!is_connected()) return "";
    
    grpc::ClientContext context;
    
    nexusd::proto::SubscribeRequest request;
    request.set_topic_pattern(pattern);
    
    auto reader = impl_->sidecar_stub->Subscribe(&context, request);
    
    nexusd::proto::Message msg;
    while (reader->Read(&msg)) {
        ReceivedMessage received;
        received.topic = msg.topic();
        received.payload = msg.payload();
        received.content_type = msg.content_type();
        received.message_id = msg.message_id();
        
        callback(received);
    }
    
    return "";  // Streaming ended
}

void GrpcClient::unsubscribe(const std::string& subscription_id) {
    if (!is_connected()) return;
    
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    
    nexusd::proto::UnsubscribeRequest request;
    request.set_subscription_id(subscription_id);
    
    nexusd::proto::UnsubscribeResponse response;
    impl_->sidecar_stub->Unsubscribe(&context, request, &response);
}

ServerInfo GrpcClient::get_server_info() {
    ServerInfo info{};
    if (!is_connected()) return info;
    
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    
    nexusd::proto::InfoRequest request;
    nexusd::proto::InfoResponse response;
    
    auto status = impl_->sidecar_stub->GetInfo(&context, request, &response);
    
    if (status.ok()) {
        info.version = response.version();
        info.instance_id = response.instance_id();
        info.uptime_seconds = response.uptime_seconds();
        info.topic_count = response.topic_count();
        info.subscriber_count = response.subscriber_count();
        info.peer_count = response.peer_count();
        info.messages_published = response.messages_published();
        info.messages_delivered = response.messages_delivered();
        info.memory_usage_bytes = response.memory_usage_bytes();
    }
    
    return info;
}

std::vector<TopicInfo> GrpcClient::get_topics() {
    std::vector<TopicInfo> topics;
    if (!is_connected()) return topics;
    
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    
    nexusd::proto::ListTopicsRequest request;
    nexusd::proto::ListTopicsResponse response;
    
    auto status = impl_->sidecar_stub->ListTopics(&context, request, &response);
    
    if (status.ok()) {
        for (const auto& t : response.topics()) {
            TopicInfo info;
            info.name = t.name();
            info.subscriber_count = t.subscriber_count();
            info.message_count = t.message_count();
            info.has_retained = t.has_retained();
            topics.push_back(std::move(info));
        }
    }
    
    return topics;
}

std::vector<ClientInfo> GrpcClient::get_clients() {
    std::vector<ClientInfo> clients;
    if (!is_connected()) return clients;
    
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    
    nexusd::proto::ListClientsRequest request;
    nexusd::proto::ListClientsResponse response;
    
    auto status = impl_->sidecar_stub->ListClients(&context, request, &response);
    
    if (status.ok()) {
        for (const auto& c : response.clients()) {
            ClientInfo info;
            info.id = c.id();
            info.address = c.address();
            info.subscription_count = c.subscription_count();
            info.messages_received = c.messages_received();
            info.connected_duration = std::chrono::seconds(c.connected_seconds());
            clients.push_back(std::move(info));
        }
    }
    
    return clients;
}

std::vector<PeerInfo> GrpcClient::get_peers() {
    std::vector<PeerInfo> peers;
    if (!is_connected()) return peers;
    
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    
    nexusd::proto::ListPeersRequest request;
    nexusd::proto::ListPeersResponse response;
    
    auto status = impl_->mesh_stub->ListPeers(&context, request, &response);
    
    if (status.ok()) {
        for (const auto& p : response.peers()) {
            PeerInfo info;
            info.id = p.id();
            info.address = p.address();
            info.status = p.status();
            info.latency_ms = p.latency_ms();
            info.messages_synced = p.messages_synced();
            peers.push_back(std::move(info));
        }
    }
    
    return peers;
}

std::string GrpcClient::get_debug_info() {
    if (!is_connected()) return "";
    
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    
    nexusd::proto::DebugRequest request;
    nexusd::proto::DebugResponse response;
    
    auto status = impl_->sidecar_stub->GetDebugInfo(&context, request, &response);
    
    return status.ok() ? response.debug_output() : "";
}

std::vector<std::pair<std::string, std::string>> GrpcClient::get_config() {
    std::vector<std::pair<std::string, std::string>> config;
    if (!is_connected()) return config;
    
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    
    nexusd::proto::GetConfigRequest request;
    nexusd::proto::GetConfigResponse response;
    
    auto status = impl_->sidecar_stub->GetConfig(&context, request, &response);
    
    if (status.ok()) {
        for (const auto& [key, value] : response.config()) {
            config.emplace_back(key, value);
        }
    }
    
    return config;
}

} // namespace nexusd::cli
