/**
 * @file discovery_agent.hpp
 * @brief UDP multicast discovery agent for peer finding.
 *
 * The DiscoveryAgent handles:
 * - Broadcasting beacons at 1Hz
 * - Receiving beacons from peers
 * - Filtering by cluster ID
 * - Triggering state sync on hash mismatch
 * - Reaping dead peers after 5s timeout
 *
 * @copyright Copyright (c) 2024 NexusD Contributors
 * @license MIT License
 */

#pragma once

#include "nexusd/core/export.hpp"
#include "nexusd/core/peer_registry.hpp"
#include "nexusd/net/udp_socket.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace nexusd {
namespace core {

/**
 * @struct DiscoveryConfig
 * @brief Configuration for the discovery agent.
 */
struct NEXUSD_CORE_API DiscoveryConfig {
    std::string cluster_id;         ///< Cluster partition key (required)
    std::string mcast_addr;         ///< Multicast group address
    uint16_t mcast_port;            ///< Multicast port
    std::string rpc_ip;             ///< This node's gRPC IP (for beacon)
    uint16_t rpc_port;              ///< This node's gRPC port (for beacon)
    int beacon_interval_ms;         ///< Beacon send interval
    int peer_timeout_ms;            ///< Peer timeout before marking dead
    int multicast_ttl;              ///< Multicast TTL (1 = local subnet)
    bool loopback;                  ///< Receive own beacons (for testing)

    DiscoveryConfig()
        : mcast_addr("239.255.77.1")
        , mcast_port(5000)
        , rpc_ip("0.0.0.0")
        , rpc_port(0)
        , beacon_interval_ms(1000)
        , peer_timeout_ms(5000)
        , multicast_ttl(1)
        , loopback(false)
    {}
};

/**
 * @brief Callback type for when a peer needs state sync.
 * Called when we detect a hash mismatch with a peer.
 */
using SyncPeerCallback = std::function<void(const PeerInfo& peer)>;

/**
 * @class DiscoveryAgent
 * @brief Handles UDP multicast discovery of mesh peers.
 *
 * Runs two background threads:
 * - Sender: Broadcasts beacons at 1Hz
 * - Receiver: Listens for peer beacons, updates registry
 *
 * Usage:
 * @code
 * DiscoveryConfig config;
 * config.cluster_id = "production";
 * config.rpc_ip = "192.168.1.100";
 * config.rpc_port = 50052;
 *
 * auto registry = std::make_shared<PeerRegistry>("node-uuid");
 * DiscoveryAgent agent(config, registry);
 * 
 * agent.setSyncCallback([](const PeerInfo& peer) {
 *     // Initiate gRPC GetNodeState call to peer
 * });
 *
 * agent.start();
 * // ... run ...
 * agent.stop();
 * @endcode
 */
class NEXUSD_CORE_API DiscoveryAgent {
public:
    /**
     * @brief Create a discovery agent.
     * @param config Discovery configuration.
     * @param registry Shared peer registry.
     */
    DiscoveryAgent(const DiscoveryConfig& config,
                   std::shared_ptr<PeerRegistry> registry);

    /**
     * @brief Destructor - stops the agent if running.
     */
    ~DiscoveryAgent();

    // Non-copyable
    DiscoveryAgent(const DiscoveryAgent&) = delete;
    DiscoveryAgent& operator=(const DiscoveryAgent&) = delete;

    /**
     * @brief Set callback for when a peer needs state synchronization.
     */
    void setSyncCallback(SyncPeerCallback callback);

    /**
     * @brief Start the discovery agent.
     * @return True if started successfully.
     */
    bool start();

    /**
     * @brief Stop the discovery agent.
     * Blocks until both threads have terminated.
     */
    void stop();

    /**
     * @brief Check if the agent is running.
     */
    bool isRunning() const { return running_.load(); }

    /**
     * @brief Get the local multicast port (for testing).
     */
    uint16_t getLocalPort() const;

    /**
     * @brief Update the RPC endpoint advertised in beacons.
     * Call this after the gRPC server starts and we know the actual port.
     */
    void updateRpcEndpoint(const std::string& ip, uint16_t port);

private:
    DiscoveryConfig config_;
    std::shared_ptr<PeerRegistry> registry_;
    SyncPeerCallback syncCallback_;

    net::UdpSocket socket_;
    std::atomic<bool> running_{false};

    std::thread senderThread_;
    std::thread receiverThread_;

    // Mutex for updating config at runtime
    mutable std::mutex configMutex_;
    std::string currentRpcIp_;
    uint16_t currentRpcPort_;

    // Thread functions
    void senderLoop();
    void receiverLoop();

    // Build and send a beacon
    void sendBeacon();

    // Process a received beacon
    void processBeacon(const uint8_t* data, size_t length,
                       const net::SocketAddress& sender);

    // Reap dead peers
    void reapDeadPeers();
};

}  // namespace core
}  // namespace nexusd
