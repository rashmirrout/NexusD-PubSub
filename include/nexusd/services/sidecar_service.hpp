/**
 * @file sidecar_service.hpp
 * @brief Async gRPC service for local application communication.
 *
 * SidecarService is the API that thin clients use:
 * - Publish: Send a message to a topic
 * - Subscribe: Receive messages for subscribed topics (streaming)
 * - Unsubscribe: Cancel a subscription
 *
 * @copyright Copyright (c) 2024 NexusD Contributors
 * @license MIT License
 */

#pragma once

#include "nexusd/services/export.hpp"
#include "nexusd/services/mesh_client.hpp"
#include "nexusd/core/peer_registry.hpp"

#include <grpcpp/grpcpp.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

// Include generated gRPC service base
#include "nexusd/proto/sidecar.grpc.pb.h"

namespace nexusd {
namespace services {

/**
 * @class SidecarServiceImpl
 * @brief Implementation of the SidecarService gRPC service.
 *
 * This is the main API for local applications:
 * - Publish: Routes messages to local and remote subscribers
 * - Subscribe: Opens a stream for receiving messages
 * - Unsubscribe: Closes a subscription
 * - GetTopics/GetPeers: Monitoring endpoints
 *
 * Usage:
 * @code
 * auto registry = std::make_shared<PeerRegistry>("node-uuid");
 * auto meshClient = std::make_shared<MeshClient>();
 * SidecarServiceImpl service(registry, meshClient);
 * 
 * grpc::ServerBuilder builder;
 * builder.AddListeningPort("127.0.0.1:50051", grpc::InsecureServerCredentials());
 * builder.RegisterService(&service);
 * auto server = builder.BuildAndStart();
 * @endcode
 */
class NEXUSD_SERVICES_API SidecarServiceImpl final : public sidecar::SidecarService::CallbackService {
public:
    /**
     * @brief Create sidecar service implementation.
     * @param registry Shared peer registry.
     * @param meshClient Client for forwarding to remote nodes.
     */
    SidecarServiceImpl(std::shared_ptr<core::PeerRegistry> registry,
                       std::shared_ptr<MeshClient> meshClient);

    ~SidecarServiceImpl() override;

    // =========================================================================
    // gRPC Service Methods (Async Callback API)
    // =========================================================================

    /**
     * @brief Handle Publish RPC.
     * Routes the message to all subscribers (local and remote).
     */
    grpc::ServerUnaryReactor* Publish(
        grpc::CallbackServerContext* context,
        const sidecar::PublishRequest* request,
        sidecar::PublishResponse* response) override;

    /**
     * @brief Handle Subscribe RPC (server-streaming).
     * Opens a stream for receiving messages on subscribed topics.
     */
    grpc::ServerWriteReactor<sidecar::MessageEvent>* Subscribe(
        grpc::CallbackServerContext* context,
        const sidecar::SubscribeRequest* request) override;

    /**
     * @brief Handle Unsubscribe RPC.
     * Closes a subscription and removes it from the registry.
     */
    grpc::ServerUnaryReactor* Unsubscribe(
        grpc::CallbackServerContext* context,
        const sidecar::UnsubscribeRequest* request,
        sidecar::UnsubscribeResponse* response) override;

    /**
     * @brief Handle GetTopics RPC.
     * Returns information about active topics.
     */
    grpc::ServerUnaryReactor* GetTopics(
        grpc::CallbackServerContext* context,
        const sidecar::GetTopicsRequest* request,
        sidecar::GetTopicsResponse* response) override;

    /**
     * @brief Handle GetPeers RPC.
     * Returns information about discovered mesh peers.
     */
    grpc::ServerUnaryReactor* GetPeers(
        grpc::CallbackServerContext* context,
        const sidecar::GetPeersRequest* request,
        sidecar::GetPeersResponse* response) override;

    // =========================================================================
    // Internal API (called by MeshService for local delivery)
    // =========================================================================

    /**
     * @brief Deliver a message received from a remote node.
     * Called by MeshServiceImpl::PushMessage.
     */
    void deliverFromRemote(const mesh::MessageEnvelope& envelope);

private:
    std::shared_ptr<core::PeerRegistry> registry_;
    std::shared_ptr<MeshClient> meshClient_;

    // Active subscription streams
    struct SubscriptionStream;
    mutable std::mutex streamsMutex_;
    std::unordered_map<std::string, std::shared_ptr<SubscriptionStream>> activeStreams_;

    // Subscription ID counter
    std::atomic<uint64_t> subscriptionCounter_{0};

    // Generate a unique subscription ID
    std::string generateSubscriptionId();

    // Forward message to remote subscribers
    int forwardToRemote(const std::string& topic, 
                        const mesh::MessageEnvelope& envelope);

    // Deliver message to local subscribers
    int deliverToLocal(const std::string& topic,
                       const mesh::MessageEnvelope& envelope);
};

}  // namespace services
}  // namespace nexusd
