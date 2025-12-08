/**
 * @file client.hpp
 * @brief NexusD C++ Client Library
 * 
 * High-level C++ client for the NexusD pub/sub sidecar daemon.
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>

namespace nexusd {

// Forward declarations
class ClientImpl;

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
    
    /// Helper to get payload as string
    std::string payloadAsString() const {
        return std::string(payload.begin(), payload.end());
    }
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
 * @brief NexusD Client
 * 
 * Synchronous client for the NexusD pub/sub daemon.
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
     */
    explicit Client(const std::string& address = "localhost:5672");
    
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
     * To stop, the callback should throw or the connection should be closed.
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
     * @brief Subscribe with error handling
     * @param topics List of topics to subscribe to
     * @param message_callback Function called for each message
     * @param error_callback Function called on error
     * @param options Subscribe options
     */
    void subscribe(
        const std::vector<std::string>& topics,
        MessageCallback message_callback,
        ErrorCallback error_callback,
        const SubscribeOptions& options = {}
    );
    
    /**
     * @brief Unsubscribe from a subscription
     * @param subscription_id The subscription ID to cancel
     * @return true if unsubscribed successfully
     */
    bool unsubscribe(const std::string& subscription_id);
    
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

private:
    std::unique_ptr<ClientImpl> impl_;
};

} // namespace nexusd
