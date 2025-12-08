/**
 * @file grpc_client.hpp
 * @brief gRPC client for connecting to NexusD daemon
 */

#pragma once

#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <chrono>

// Forward declarations for gRPC types
namespace grpc {
    class Channel;
}

namespace nexusd::cli {

/**
 * @brief Message from subscription
 */
struct ReceivedMessage {
    std::string topic;
    std::string payload;
    std::string content_type;
    std::string message_id;
    std::chrono::system_clock::time_point timestamp;
};

/**
 * @brief Topic information
 */
struct TopicInfo {
    std::string name;
    int subscriber_count;
    int64_t message_count;
    bool has_retained;
};

/**
 * @brief Peer information
 */
struct PeerInfo {
    std::string id;
    std::string address;
    std::string status;
    int64_t latency_ms;
    int64_t messages_synced;
};

/**
 * @brief Client (subscriber) information
 */
struct ClientInfo {
    std::string id;
    std::string address;
    int subscription_count;
    int64_t messages_received;
    std::chrono::seconds connected_duration;
};

/**
 * @brief Daemon server information
 */
struct ServerInfo {
    std::string version;
    std::string instance_id;
    int64_t uptime_seconds;
    int topic_count;
    int subscriber_count;
    int peer_count;
    int64_t messages_published;
    int64_t messages_delivered;
    size_t memory_usage_bytes;
};

/**
 * @brief gRPC client for NexusD daemon
 */
class GrpcClient {
public:
    GrpcClient();
    ~GrpcClient();
    
    /**
     * @brief Connect to NexusD daemon
     * @param address Host:port address
     * @return true if connected successfully
     */
    bool connect(const std::string& address);
    
    /**
     * @brief Disconnect from daemon
     */
    void disconnect();
    
    /**
     * @brief Check if connected
     */
    bool is_connected() const;
    
    /**
     * @brief Get connected address
     */
    std::string get_address() const;
    
    /**
     * @brief Ping the server
     * @return Round-trip time in milliseconds, or -1 if failed
     */
    int64_t ping();
    
    // Pub/Sub operations
    /**
     * @brief Publish a message
     * @return Message ID or empty string on failure
     */
    std::string publish(const std::string& topic, const std::string& payload,
                        const std::string& content_type = "text/plain",
                        bool retain = false);
    
    /**
     * @brief Subscribe to a topic pattern
     * @param pattern Topic pattern (supports wildcards)
     * @param callback Message callback
     * @return Subscription ID or empty string on failure
     */
    std::string subscribe(const std::string& pattern,
                          std::function<void(const ReceivedMessage&)> callback);
    
    /**
     * @brief Unsubscribe from a topic
     * @param subscription_id Subscription ID from subscribe()
     */
    void unsubscribe(const std::string& subscription_id);
    
    // Info queries
    ServerInfo get_server_info();
    std::vector<TopicInfo> get_topics();
    std::vector<ClientInfo> get_clients();
    std::vector<PeerInfo> get_peers();
    
    // Debug operations
    std::string get_debug_info();
    std::vector<std::pair<std::string, std::string>> get_config();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nexusd::cli
