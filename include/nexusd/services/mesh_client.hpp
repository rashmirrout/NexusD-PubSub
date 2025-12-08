/**
 * @file mesh_client.hpp
 * @brief Async gRPC client for inter-node communication.
 *
 * MeshClient manages gRPC channels and stubs for calling
 * MeshService on remote peer nodes.
 *
 * @copyright Copyright (c) 2024 NexusD Contributors
 * @license MIT License
 */

#pragma once

#include "nexusd/services/export.hpp"
#include "nexusd/core/peer_registry.hpp"

#include <grpcpp/grpcpp.h>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

// Forward declare generated types
namespace nexusd {
namespace mesh {
class MeshService;
class NodeState;
class MessageEnvelope;
class Ack;
}
}

namespace nexusd {
namespace services {

/**
 * @class MeshClient
 * @brief Manages gRPC client connections to peer nodes.
 *
 * Creates and caches gRPC channels/stubs for each peer.
 * Provides async methods for calling MeshService on remote nodes.
 *
 * Usage:
 * @code
 * MeshClient client;
 * 
 * // Get node state from a peer
 * client.getNodeState("192.168.1.100:50052",
 *     [](bool ok, const NodeState& state) {
 *         if (ok) { ... }
 *     });
 *
 * // Push a message to a peer
 * client.pushMessage("192.168.1.100:50052", envelope,
 *     [](bool ok, const Ack& ack) {
 *         if (ok) { ... }
 *     });
 * @endcode
 */
class NEXUSD_SERVICES_API MeshClient {
public:
    using GetNodeStateCallback = std::function<void(bool success, 
                                                    const mesh::NodeState& state)>;
    using PushMessageCallback = std::function<void(bool success,
                                                   const mesh::Ack& ack)>;

    /**
     * @brief Create a mesh client.
     */
    MeshClient();

    /**
     * @brief Destructor.
     */
    ~MeshClient();

    // Non-copyable
    MeshClient(const MeshClient&) = delete;
    MeshClient& operator=(const MeshClient&) = delete;

    /**
     * @brief Get the node state from a remote peer.
     * @param endpoint Peer's gRPC endpoint (ip:port).
     * @param callback Called with result.
     */
    void getNodeState(const std::string& endpoint, GetNodeStateCallback callback);

    /**
     * @brief Push a message to a remote peer.
     * @param endpoint Peer's gRPC endpoint (ip:port).
     * @param envelope The message to send.
     * @param callback Called with result.
     */
    void pushMessage(const std::string& endpoint,
                     const mesh::MessageEnvelope& envelope,
                     PushMessageCallback callback);

    /**
     * @brief Push a message synchronously (blocking).
     * @param endpoint Peer's gRPC endpoint.
     * @param envelope The message to send.
     * @return True if the push succeeded.
     */
    bool pushMessageSync(const std::string& endpoint,
                         const mesh::MessageEnvelope& envelope);

    /**
     * @brief Remove cached channel for an endpoint.
     * Call when a peer dies to clean up resources.
     */
    void removeChannel(const std::string& endpoint);

    /**
     * @brief Clear all cached channels.
     */
    void clearChannels();

private:
    struct ChannelEntry {
        std::shared_ptr<grpc::Channel> channel;
        std::unique_ptr<mesh::MeshService::Stub> stub;
    };

    mutable std::mutex channelMutex_;
    std::unordered_map<std::string, ChannelEntry> channels_;

    // Get or create channel/stub for an endpoint
    mesh::MeshService::Stub* getStub(const std::string& endpoint);
};

}  // namespace services
}  // namespace nexusd
