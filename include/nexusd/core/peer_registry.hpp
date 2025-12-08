/**
 * @file peer_registry.hpp
 * @brief Thread-safe peer and routing table management.
 *
 * The PeerRegistry maintains:
 * - Known peers and their connection state
 * - Global routing table (Topic -> Set of Nodes)
 * - Retained messages for late-joiner support
 * - Local subscriptions for the sidecar service
 *
 * @copyright Copyright (c) 2024 NexusD Contributors
 * @license MIT License
 */

#pragma once

#include "nexusd/core/export.hpp"
#include "nexusd/utils/crc64.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace nexusd {

// Forward declarations for gRPC types
namespace mesh {
class MeshService;
}

namespace core {

/**
 * @enum PeerStatus
 * @brief Health status of a remote peer.
 */
enum class PeerStatus {
    ALIVE,      ///< Receiving beacons regularly
    SUSPECTED,  ///< Missed one beacon (grace period)
    DEAD        ///< No beacons for 5+ seconds
};

inline const char* peerStatusToString(PeerStatus status) {
    switch (status) {
        case PeerStatus::ALIVE: return "alive";
        case PeerStatus::SUSPECTED: return "suspected";
        case PeerStatus::DEAD: return "dead";
        default: return "unknown";
    }
}

/**
 * @struct PeerInfo
 * @brief Information about a discovered remote peer.
 */
struct NEXUSD_CORE_API PeerInfo {
    std::string instance_uuid;      ///< Peer's unique session ID
    std::string cluster_id;         ///< Peer's cluster partition
    std::string rpc_ip;             ///< Peer's gRPC IP
    uint16_t rpc_port;              ///< Peer's gRPC port
    uint64_t topic_state_hash;      ///< Peer's subscription hash
    std::vector<std::string> topics;///< Peer's subscribed topics (after sync)
    PeerStatus status;              ///< Peer's health status
    std::chrono::steady_clock::time_point last_seen;  ///< Last beacon time
    bool synced;                    ///< Have we pulled their state?

    PeerInfo()
        : rpc_port(0)
        , topic_state_hash(0)
        , status(PeerStatus::ALIVE)
        , last_seen(std::chrono::steady_clock::now())
        , synced(false)
    {}

    std::string endpoint() const {
        return rpc_ip + ":" + std::to_string(rpc_port);
    }
};

/**
 * @struct RetainedMessage
 * @brief A retained message for late-joiner support.
 */
struct NEXUSD_CORE_API RetainedMessage {
    std::string message_id;
    std::string topic;
    std::vector<uint8_t> payload;
    std::string content_type;
    std::string source_node_id;
    int64_t timestamp_ms;
    int64_t ttl_ms;
};

/**
 * @struct LocalSubscription
 * @brief A subscription from a local application.
 */
struct NEXUSD_CORE_API LocalSubscription {
    std::string subscription_id;
    std::string client_id;
    std::vector<std::string> topics;
    int32_t max_buffer_size;
    std::chrono::steady_clock::time_point created_at;
    
    // Callback to deliver messages (set by SidecarService)
    std::function<void(const RetainedMessage&)> deliverCallback;
};

/**
 * @class PeerRegistry
 * @brief Thread-safe registry for peers, routing, and subscriptions.
 *
 * This is the central state management component for the mesh.
 * All access is thread-safe using a read-write lock (shared_mutex).
 *
 * Usage:
 * @code
 * PeerRegistry registry("my-node-uuid");
 * 
 * // Update peer from beacon
 * registry.upsertPeer(peerInfo);
 * 
 * // Add local subscription
 * registry.addLocalSubscription("sub-123", {"topic/a", "topic/b"}, callback);
 * 
 * // Get routing destinations for a topic
 * auto peers = registry.getRemoteSubscribers("topic/a");
 * @endcode
 */
class NEXUSD_CORE_API PeerRegistry {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    /**
     * @brief Create a registry for the local node.
     * @param localNodeId This node's instance UUID.
     */
    explicit PeerRegistry(const std::string& localNodeId);

    ~PeerRegistry() = default;

    // Non-copyable
    PeerRegistry(const PeerRegistry&) = delete;
    PeerRegistry& operator=(const PeerRegistry&) = delete;

    // =========================================================================
    // Peer Management
    // =========================================================================

    /**
     * @brief Add or update a peer from a received beacon.
     * @param info Peer information from the beacon.
     * @return True if this is a new peer or hash changed (need sync).
     */
    bool upsertPeer(const PeerInfo& info);

    /**
     * @brief Remove a peer from the registry.
     * @param instanceUuid The peer's instance UUID.
     */
    void removePeer(const std::string& instanceUuid);

    /**
     * @brief Get information about a specific peer.
     * @param instanceUuid The peer's instance UUID.
     * @return Peer info, or nullptr if not found.
     */
    std::shared_ptr<PeerInfo> getPeer(const std::string& instanceUuid) const;

    /**
     * @brief Get all known peers.
     */
    std::vector<PeerInfo> getAllPeers() const;

    /**
     * @brief Update a peer's subscription state after sync.
     * @param instanceUuid The peer's instance UUID.
     * @param topics The peer's subscribed topics.
     * @param hash The peer's topic hash.
     */
    void updatePeerState(const std::string& instanceUuid,
                         const std::vector<std::string>& topics,
                         uint64_t hash);

    /**
     * @brief Find peers that have timed out.
     * @param timeout Duration after which a peer is considered dead.
     * @return List of dead peer UUIDs.
     */
    std::vector<std::string> reapDeadPeers(std::chrono::milliseconds timeout);

    // =========================================================================
    // Local Subscription Management
    // =========================================================================

    /**
     * @brief Add a local subscription.
     * @param subscriptionId Unique subscription ID.
     * @param clientId Client identifier.
     * @param topics Topics to subscribe to.
     * @param maxBufferSize Max messages to buffer.
     * @param callback Delivery callback.
     * @return True on success.
     */
    bool addLocalSubscription(
        const std::string& subscriptionId,
        const std::string& clientId,
        const std::vector<std::string>& topics,
        int32_t maxBufferSize,
        std::function<void(const RetainedMessage&)> callback);

    /**
     * @brief Remove a local subscription.
     * @param subscriptionId The subscription to remove.
     * @return True if found and removed.
     */
    bool removeLocalSubscription(const std::string& subscriptionId);

    /**
     * @brief Get local subscriptions for a topic.
     */
    std::vector<std::shared_ptr<LocalSubscription>> 
        getLocalSubscriptions(const std::string& topic) const;

    /**
     * @brief Get all locally subscribed topics.
     */
    std::vector<std::string> getLocalTopics() const;

    // =========================================================================
    // Routing Table
    // =========================================================================

    /**
     * @brief Get remote nodes that subscribe to a topic.
     * @param topic The topic to look up.
     * @return List of peer instance UUIDs.
     */
    std::vector<std::string> getRemoteSubscribers(const std::string& topic) const;

    /**
     * @brief Check if anyone (local or remote) subscribes to a topic.
     */
    bool hasSubscribers(const std::string& topic) const;

    /**
     * @brief Get the full routing table.
     */
    std::unordered_map<std::string, std::unordered_set<std::string>> 
        getRoutingTable() const;

    // =========================================================================
    // Topic Hash
    // =========================================================================

    /**
     * @brief Get the current topic state hash for this node.
     * Computed over sorted local subscription topics.
     */
    uint64_t getLocalTopicHash() const;

    // =========================================================================
    // Retained Messages
    // =========================================================================

    /**
     * @brief Store a retained message.
     * @param topic The topic.
     * @param message The message to retain.
     */
    void setRetainedMessage(const std::string& topic, const RetainedMessage& message);

    /**
     * @brief Get a retained message for a topic.
     * @return The retained message, or nullptr if none.
     */
    std::shared_ptr<RetainedMessage> getRetainedMessage(const std::string& topic) const;

    /**
     * @brief Get all retained messages.
     */
    std::unordered_map<std::string, RetainedMessage> getAllRetainedMessages() const;

    /**
     * @brief Clear expired retained messages.
     */
    void clearExpiredRetainedMessages();

    // =========================================================================
    // Node Info
    // =========================================================================

    /**
     * @brief Get this node's instance UUID.
     */
    const std::string& getLocalNodeId() const { return localNodeId_; }

private:
    std::string localNodeId_;

    // Peer map: instance_uuid -> PeerInfo
    mutable std::shared_mutex peerMutex_;
    std::unordered_map<std::string, std::shared_ptr<PeerInfo>> peers_;

    // Local subscriptions: subscription_id -> LocalSubscription
    mutable std::shared_mutex subscriptionMutex_;
    std::unordered_map<std::string, std::shared_ptr<LocalSubscription>> localSubscriptions_;
    
    // Topic -> subscription IDs (for fast lookup)
    std::unordered_map<std::string, std::unordered_set<std::string>> topicToSubscriptions_;
    
    // Cached local topic hash
    mutable std::atomic<uint64_t> cachedLocalHash_{0};
    mutable std::atomic<bool> hashDirty_{true};

    // Routing table: topic -> set of peer UUIDs
    // Rebuilt when peer state changes
    mutable std::shared_mutex routingMutex_;
    std::unordered_map<std::string, std::unordered_set<std::string>> routingTable_;

    // Retained messages: topic -> message
    mutable std::shared_mutex retainedMutex_;
    std::unordered_map<std::string, RetainedMessage> retainedMessages_;

    // Rebuild the routing table from peer state
    void rebuildRoutingTable();
    
    // Compute the local topic hash
    uint64_t computeLocalTopicHash() const;
};

}  // namespace core
}  // namespace nexusd
