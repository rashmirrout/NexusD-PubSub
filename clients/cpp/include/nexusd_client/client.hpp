/**
 * @file client.hpp
 * @brief NexusD C++ Client Library
 * 
 * High-level C++ client for the NexusD pub/sub sidecar daemon.
 * Features automatic reconnection, gap detection, and message replay.
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>
#include <chrono>

namespace nexusd {

// Forward declarations
class ClientImpl;

/**
 * @brief Gap recovery mode for ResumeSubscribe
 */
enum class GapRecoveryMode {
    None = 0,           ///< No gap recovery
    RetainedOnly = 1,   ///< Only deliver retained message
    ReplayBuffer = 2    ///< Replay all buffered messages
};

/**
 * @brief Configuration for automatic reconnection
 */
struct ReconnectConfig {
    bool enabled = true;
    std::chrono::milliseconds initial_delay{100};
    std::chrono::milliseconds max_delay{30000};
    double multiplier = 2.0;
    uint32_t max_attempts = 0;  ///< 0 = unlimited
};

/**
 * @brief Received message structure
 */
struct Message {
    std::string message_id;
    std::string topic;
    std::vector<uint8_t> payload;
    std::string content_type;
    int64_t timestamp_ms = 0;
    std::string source_node_id;
    std::string correlation_id;
    bool is_retained = false;
    bool is_replay = false;
    uint64_t sequence_number = 0;
    
    /// Helper to get payload as string
    std::string payloadAsString() const {
        return std::string(payload.begin(), payload.end());
    }
};

/**
 * @brief Information about an active subscription
 */
struct SubscriptionInfo {
    std::string subscription_id;
    std::vector<std::string> topics;
    bool gap_detected = false;
    uint64_t missed_message_count = 0;
    bool replay_started = false;
    uint64_t last_sequence = 0;
};

/**
 * @brief Result of a publish operation
 */
struct PublishResult {
    bool success = false;
    std::string message_id;
    int32_t subscriber_count = 0;
    std::string error_message;
};

/**
 * @brief Options for publish operations
 */
struct PublishOptions {
    std::string content_type;
    bool retain = false;
    std::string correlation_id;
    int64_t ttl_ms = 0;
};

/**
 * @brief Options for subscribe operations
 */
struct SubscribeOptions {
    std::string client_id;
    bool receive_retained = true;
    int32_t max_buffer_size = 0;
    GapRecoveryMode gap_recovery_mode = GapRecoveryMode::ReplayBuffer;
};

/**
 * @brief Topic information
 */
struct TopicInfo {
    std::string topic;
    int32_t subscriber_count = 0;
    bool has_retained = false;
};

/**
 * @brief Mesh peer information
 */
struct PeerInfo {
    std::string node_id;
    std::string address;
    std::string cluster_id;
    bool is_healthy = false;
};

/**
 * @brief Callback type for received messages
 */
using MessageCallback = std::function<void(const Message&)>;

/**
 * @brief Callback type for subscription errors
 */
using ErrorCallback = std::function<void(const std::string&)>;

/**
 * @brief Callback type for gap detection
 */
using GapCallback = std::function<void(const SubscriptionInfo&)>;

/**
 * @brief NexusD Client
 * 
 * Synchronous client for the NexusD pub/sub daemon.
 * Features automatic reconnection with exponential backoff.
 * 
 * Example:
 * @code
 * nexusd::Client client("localhost:5672");
 * 
 * // Publish
 * auto result = client.publish("topic", "hello");
 * 
 * // Subscribe (blocking)
 * client.subscribe({"topic"}, [](const nexusd::Message& msg) {
 *     std::cout << msg.topic << ": " << msg.payloadAsString() << "\n";
 * });
 * @endcode
 */
class Client {
public:
    /**
     * @brief Construct a new client
     * @param address Daemon address in "host:port" format
     * @param reconnect_config Reconnection configuration
     */
    explicit Client(
        const std::string& address = "localhost:5672",
        const ReconnectConfig& reconnect_config = {}
    );
    
    /// Destructor
    ~Client();
    
    // Non-copyable
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    
    // Movable
    Client(Client&&) noexcept;
    Client& operator=(Client&&) noexcept;
    
    /**
     * @brief Publish a message to a topic
     * @param topic Target topic
     * @param payload Message payload as bytes
     * @param options Publish options
     * @return PublishResult with success status
     */
    PublishResult publish(
        const std::string& topic,
        const std::vector<uint8_t>& payload,
        const PublishOptions& options = {}
    );
    
    /**
     * @brief Publish a string message to a topic
     * @param topic Target topic
     * @param payload Message payload as string
     * @param options Publish options
     * @return PublishResult with success status
     */
    PublishResult publish(
        const std::string& topic,
        const std::string& payload,
        const PublishOptions& options = {}
    );
    
    /**
     * @brief Subscribe to topics
     * 
     * This method blocks and calls the callback for each message.
     * Features automatic reconnection with gap recovery.
     * 
     * @param topics List of topics to subscribe to
     * @param callback Function called for each received message
     * @param options Subscribe options
     */
    void subscribe(
        const std::vector<std::string>& topics,
        MessageCallback callback,
        const SubscribeOptions& options = {}
    );
    
    /**
     * @brief Subscribe with error and gap handling
     * @param topics List of topics to subscribe to
     * @param message_callback Function called for each message
     * @param error_callback Function called on error
     * @param gap_callback Function called when gap is detected
     * @param options Subscribe options
     */
    void subscribe(
        const std::vector<std::string>& topics,
        MessageCallback message_callback,
        ErrorCallback error_callback,
        GapCallback gap_callback = nullptr,
        const SubscribeOptions& options = {}
    );
    
    /**
     * @brief Unsubscribe from a subscription
     * @param subscription_id The subscription ID to cancel
     * @param pause If true, pause for later resumption
     * @return true if unsubscribed successfully
     */
    bool unsubscribe(const std::string& subscription_id, bool pause = false);
    
    /**
     * @brief Resume a paused subscription
     * @param subscription_id The subscription ID to resume
     * @param last_sequence Last sequence number received
     * @param callback Message callback
     * @param gap_recovery_mode How to handle gaps
     */
    void resumeSubscribe(
        const std::string& subscription_id,
        uint64_t last_sequence,
        MessageCallback callback,
        GapRecoveryMode gap_recovery_mode = GapRecoveryMode::ReplayBuffer
    );
    
    /**
     * @brief Get list of topics with subscriber counts
     * @return Vector of TopicInfo
     */
    std::vector<TopicInfo> getTopics();
    
    /**
     * @brief Get list of mesh peers
     * @return Vector of PeerInfo
     */
    std::vector<PeerInfo> getPeers();
    
    /**
     * @brief Check if connected to daemon
     * @return true if connected
     */
    bool isConnected() const;
    
    /**
     * @brief Get active subscriptions
     */
    std::vector<SubscriptionInfo> getActiveSubscriptions() const;

private:
    std::unique_ptr<ClientImpl> impl_;
};

} // namespace nexusd
