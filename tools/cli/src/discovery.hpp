/**
 * @file discovery.hpp
 * @brief UDP multicast discovery for NexusD instances
 */

#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <cstdint>

namespace nexusd::cli {

/**
 * @brief Discovered NexusD instance information
 */
struct DiscoveredInstance {
    std::string host;
    uint16_t port;
    std::string instance_id;
    std::string version;
    std::string status;
    int64_t uptime_seconds;
    int topic_count;
    int subscriber_count;
    int peer_count;
    std::chrono::steady_clock::time_point discovered_at;
    
    std::string address() const {
        return host + ":" + std::to_string(port);
    }
};

/**
 * @brief Discovery configuration
 */
struct DiscoveryConfig {
    std::string multicast_group = "239.255.77.77";
    uint16_t multicast_port = 5670;
    std::chrono::milliseconds timeout = std::chrono::milliseconds(2000);
    int max_instances = 100;
};

/**
 * @brief UDP multicast discovery for finding NexusD instances
 */
class Discovery {
public:
    explicit Discovery(DiscoveryConfig config = {});
    ~Discovery();
    
    /**
     * @brief Discover all NexusD instances on the network
     * @return Vector of discovered instances
     */
    std::vector<DiscoveredInstance> discover();
    
    /**
     * @brief Discover instances with custom timeout
     * @param timeout Discovery timeout
     * @return Vector of discovered instances
     */
    std::vector<DiscoveredInstance> discover(std::chrono::milliseconds timeout);
    
    /**
     * @brief Get cached instances from last discovery
     * @return Vector of cached instances
     */
    const std::vector<DiscoveredInstance>& get_cached() const;
    
    /**
     * @brief Get instance by index (1-based, for user convenience)
     * @param index 1-based index
     * @return Pointer to instance or nullptr if not found
     */
    const DiscoveredInstance* get_by_index(int index) const;
    
    /**
     * @brief Get instance by port
     * @param port Port number
     * @return Pointer to instance or nullptr if not found
     */
    const DiscoveredInstance* get_by_port(uint16_t port) const;
    
    /**
     * @brief Clear cached instances
     */
    void clear_cache();

private:
    DiscoveryConfig config_;
    std::vector<DiscoveredInstance> cached_instances_;
    
    void init_socket();
    void close_socket();
    void send_discovery_request();
    bool receive_response(DiscoveredInstance& instance, std::chrono::milliseconds remaining);
    
#ifdef _WIN32
    using socket_t = unsigned long long;
#else
    using socket_t = int;
#endif
    socket_t socket_ = static_cast<socket_t>(-1);
    bool socket_initialized_ = false;
};

} // namespace nexusd::cli
