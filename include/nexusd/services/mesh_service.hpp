/**
 * @file mesh_service.hpp
 * @brief Async gRPC service for inter-node communication.
 *
 * MeshService handles incoming RPCs from other daemons:
 * - GetNodeState: Returns this node's subscription state
 * - PushMessage: Receives messages from remote publishers
 *
 * @copyright Copyright (c) 2024 NexusD Contributors
 * @license MIT License
 */

#pragma once

#include "nexusd/services/export.hpp"
#include "nexusd/core/peer_registry.hpp"

#include <grpcpp/grpcpp.h>
#include <memory>

// Include generated gRPC service base
#include "nexusd/proto/mesh.grpc.pb.h"

namespace nexusd {
namespace services {

// Forward declaration
class SidecarService;

/**
 * @class MeshServiceImpl
 * @brief Implementation of the MeshService gRPC service.
 *
 * Handles inter-node communication:
 * - GetNodeState: Called by peers during anti-entropy sync
 * - PushMessage: Called when a remote publisher sends us a message
 *
 * Usage:
 * @code
 * auto registry = std::make_shared<PeerRegistry>("node-uuid");
 * MeshServiceImpl service(registry);
 * 
 * grpc::ServerBuilder builder;
 * builder.AddListeningPort("0.0.0.0:50052", grpc::InsecureServerCredentials());
 * builder.RegisterService(&service);
 * auto server = builder.BuildAndStart();
 * @endcode
 */
class NEXUSD_SERVICES_API MeshServiceImpl final : public mesh::MeshService::CallbackService {
public:
    /**
     * @brief Create mesh service implementation.
     * @param registry Shared peer registry.
     */
    explicit MeshServiceImpl(std::shared_ptr<core::PeerRegistry> registry);

    ~MeshServiceImpl() override = default;

    /**
     * @brief Set the sidecar service for local message delivery.
     * Must be called before processing PushMessage requests.
     */
    void setSidecarService(SidecarService* sidecar);

    // =========================================================================
    // gRPC Service Methods (Async Callback API)
    // =========================================================================

    /**
     * @brief Handle GetNodeState RPC.
     * Returns this node's current subscription state for anti-entropy sync.
     */
    grpc::ServerUnaryReactor* GetNodeState(
        grpc::CallbackServerContext* context,
        const google::protobuf::Empty* request,
        mesh::NodeState* response) override;

    /**
     * @brief Handle PushMessage RPC.
     * Receives a message from a remote publisher and delivers locally.
     */
    grpc::ServerUnaryReactor* PushMessage(
        grpc::CallbackServerContext* context,
        const mesh::MessageEnvelope* request,
        mesh::Ack* response) override;

    /**
     * @brief Handle PushMessageStream RPC (bidirectional streaming).
     * For high-throughput scenarios.
     */
    grpc::ServerBidiReactor<mesh::MessageEnvelope, mesh::Ack>* PushMessageStream(
        grpc::CallbackServerContext* context) override;

private:
    std::shared_ptr<core::PeerRegistry> registry_;
    SidecarService* sidecarService_{nullptr};

    // Deliver a message to local subscribers
    void deliverLocally(const mesh::MessageEnvelope& envelope);
};

}  // namespace services
}  // namespace nexusd
