/**
 * @file mesh_client.cpp
 * @brief MeshClient implementation.
 *
 * @copyright Copyright (c) 2024 NexusD Contributors
 * @license MIT License
 */

#include "nexusd/services/mesh_client.hpp"
#include "nexusd/utils/logger.hpp"

#include "nexusd/proto/mesh.grpc.pb.h"

namespace nexusd {
namespace services {

MeshClient::MeshClient() {
    LOG_DEBUG("MeshClient", "Created mesh client");
}

MeshClient::~MeshClient() {
    clearChannels();
}

mesh::MeshService::Stub* MeshClient::getStub(const std::string& endpoint) {
    std::lock_guard<std::mutex> lock(channelMutex_);

    auto it = channels_.find(endpoint);
    if (it != channels_.end()) {
        return it->second.stub.get();
    }

    // Create new channel
    LOG_DEBUG("MeshClient", "Creating channel to {}", endpoint);
    
    grpc::ChannelArguments args;
    args.SetInt(GRPC_ARG_KEEPALIVE_TIME_MS, 10000);
    args.SetInt(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 5000);
    args.SetInt(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA, 0);
    
    auto channel = grpc::CreateCustomChannel(
        endpoint,
        grpc::InsecureChannelCredentials(),
        args);

    ChannelEntry entry;
    entry.channel = channel;
    entry.stub = mesh::MeshService::NewStub(channel);

    auto* stub = entry.stub.get();
    channels_[endpoint] = std::move(entry);

    return stub;
}

void MeshClient::getNodeState(const std::string& endpoint, 
                               GetNodeStateCallback callback) {
    auto* stub = getStub(endpoint);
    if (!stub) {
        mesh::NodeState empty;
        callback(false, empty);
        return;
    }

    // Create async call context
    auto context = std::make_unique<grpc::ClientContext>();
    context->set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));

    google::protobuf::Empty request;
    auto response = std::make_shared<mesh::NodeState>();
    auto status = std::make_shared<grpc::Status>();

    // Make async unary call
    stub->async()->GetNodeState(
        context.get(),
        &request,
        response.get(),
        [callback, response, context = std::move(context)](grpc::Status s) {
            callback(s.ok(), *response);
            if (!s.ok()) {
                LOG_WARN("MeshClient", "GetNodeState failed: {}", s.error_message());
            }
        });
}

void MeshClient::pushMessage(const std::string& endpoint,
                              const mesh::MessageEnvelope& envelope,
                              PushMessageCallback callback) {
    auto* stub = getStub(endpoint);
    if (!stub) {
        mesh::Ack ack;
        ack.set_success(false);
        ack.set_error_message("Failed to get stub");
        callback(false, ack);
        return;
    }

    auto context = std::make_unique<grpc::ClientContext>();
    context->set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));

    auto response = std::make_shared<mesh::Ack>();

    stub->async()->PushMessage(
        context.get(),
        &envelope,
        response.get(),
        [callback, response, endpoint, context = std::move(context)](grpc::Status s) {
            if (s.ok()) {
                callback(true, *response);
            } else {
                LOG_WARN("MeshClient", "PushMessage to {} failed: {}", 
                         endpoint, s.error_message());
                mesh::Ack ack;
                ack.set_success(false);
                ack.set_error_message(s.error_message());
                callback(false, ack);
            }
        });
}

bool MeshClient::pushMessageSync(const std::string& endpoint,
                                  const mesh::MessageEnvelope& envelope) {
    auto* stub = getStub(endpoint);
    if (!stub) {
        return false;
    }

    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));

    mesh::Ack response;
    grpc::Status status = stub->PushMessage(&context, envelope, &response);

    if (!status.ok()) {
        LOG_WARN("MeshClient", "PushMessage sync to {} failed: {}",
                 endpoint, status.error_message());
        return false;
    }

    return response.success();
}

void MeshClient::removeChannel(const std::string& endpoint) {
    std::lock_guard<std::mutex> lock(channelMutex_);
    auto it = channels_.find(endpoint);
    if (it != channels_.end()) {
        LOG_DEBUG("MeshClient", "Removing channel to {}", endpoint);
        channels_.erase(it);
    }
}

void MeshClient::clearChannels() {
    std::lock_guard<std::mutex> lock(channelMutex_);
    LOG_DEBUG("MeshClient", "Clearing all channels");
    channels_.clear();
}

}  // namespace services
}  // namespace nexusd
