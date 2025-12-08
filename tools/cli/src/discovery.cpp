/**
 * @file discovery.cpp
 * @brief UDP multicast discovery implementation
 */

#include "discovery.hpp"
#include <cstring>
#include <stdexcept>
#include <algorithm>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    
    namespace {
        struct WinsockInit {
            WinsockInit() {
                WSADATA wsa;
                WSAStartup(MAKEWORD(2, 2), &wsa);
            }
            ~WinsockInit() {
                WSACleanup();
            }
        };
        static WinsockInit winsock_init;
    }
    
    #define CLOSE_SOCKET closesocket
    #define SOCKET_ERROR_CODE WSAGetLastError()
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <poll.h>
    #include <errno.h>
    
    #define CLOSE_SOCKET close
    #define SOCKET_ERROR_CODE errno
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

#include <sstream>

namespace nexusd::cli {

// Discovery protocol magic bytes
static constexpr uint8_t DISCOVERY_MAGIC[] = {'N', 'X', 'D', 'S'};
static constexpr uint8_t DISCOVERY_VERSION = 1;

// Discovery message types
enum class DiscoveryMessageType : uint8_t {
    REQUEST = 0x01,
    RESPONSE = 0x02
};

Discovery::Discovery(DiscoveryConfig config)
    : config_(std::move(config)) {}

Discovery::~Discovery() {
    close_socket();
}

void Discovery::init_socket() {
    if (socket_initialized_) return;
    
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == INVALID_SOCKET) {
        throw std::runtime_error("Failed to create discovery socket");
    }
    
    // Allow address reuse
    int reuse = 1;
    setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, 
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    
    // Set receive timeout
    #ifdef _WIN32
        DWORD timeout_ms = static_cast<DWORD>(config_.timeout.count());
        setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
    #else
        struct timeval tv;
        tv.tv_sec = config_.timeout.count() / 1000;
        tv.tv_usec = (config_.timeout.count() % 1000) * 1000;
        setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&tv), sizeof(tv));
    #endif
    
    // Enable broadcast
    int broadcast = 1;
    setsockopt(socket_, SOL_SOCKET, SO_BROADCAST,
               reinterpret_cast<const char*>(&broadcast), sizeof(broadcast));
    
    // Join multicast group for receiving responses
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(config_.multicast_group.c_str());
    mreq.imr_interface.s_addr = INADDR_ANY;
    setsockopt(socket_, IPPROTO_IP, IP_ADD_MEMBERSHIP,
               reinterpret_cast<const char*>(&mreq), sizeof(mreq));
    
    // Bind to any port for receiving
    struct sockaddr_in local_addr;
    std::memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = 0;  // Let OS assign port
    
    if (bind(socket_, reinterpret_cast<struct sockaddr*>(&local_addr), 
             sizeof(local_addr)) == SOCKET_ERROR) {
        close_socket();
        throw std::runtime_error("Failed to bind discovery socket");
    }
    
    socket_initialized_ = true;
}

void Discovery::close_socket() {
    if (socket_initialized_ && socket_ != INVALID_SOCKET) {
        CLOSE_SOCKET(socket_);
        socket_ = INVALID_SOCKET;
        socket_initialized_ = false;
    }
}

void Discovery::send_discovery_request() {
    // Build discovery request packet
    // Format: MAGIC(4) | VERSION(1) | TYPE(1) | Reserved(2)
    uint8_t packet[8];
    std::memcpy(packet, DISCOVERY_MAGIC, 4);
    packet[4] = DISCOVERY_VERSION;
    packet[5] = static_cast<uint8_t>(DiscoveryMessageType::REQUEST);
    packet[6] = 0;
    packet[7] = 0;
    
    struct sockaddr_in dest_addr;
    std::memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_addr.s_addr = inet_addr(config_.multicast_group.c_str());
    dest_addr.sin_port = htons(config_.multicast_port);
    
    sendto(socket_, reinterpret_cast<const char*>(packet), sizeof(packet), 0,
           reinterpret_cast<struct sockaddr*>(&dest_addr), sizeof(dest_addr));
}

bool Discovery::receive_response(DiscoveredInstance& instance, 
                                  std::chrono::milliseconds remaining) {
    // Set timeout for this receive
    #ifdef _WIN32
        DWORD timeout_ms = static_cast<DWORD>(remaining.count());
        setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
    #else
        struct timeval tv;
        tv.tv_sec = remaining.count() / 1000;
        tv.tv_usec = (remaining.count() % 1000) * 1000;
        setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&tv), sizeof(tv));
    #endif
    
    char buffer[1024];
    struct sockaddr_in sender_addr;
    socklen_t sender_len = sizeof(sender_addr);
    
    auto recv_result = recvfrom(socket_, buffer, sizeof(buffer) - 1, 0,
                                 reinterpret_cast<struct sockaddr*>(&sender_addr), 
                                 &sender_len);
    
    if (recv_result <= 0) {
        return false;
    }
    
    // Validate magic bytes
    if (recv_result < 8 || std::memcmp(buffer, DISCOVERY_MAGIC, 4) != 0) {
        return false;
    }
    
    // Check message type
    if (buffer[5] != static_cast<uint8_t>(DiscoveryMessageType::RESPONSE)) {
        return false;
    }
    
    // Parse response payload (JSON-like format after header)
    // Format: MAGIC(4) | VERSION(1) | TYPE(1) | PORT(2) | PAYLOAD...
    if (recv_result < 10) {
        return false;
    }
    
    instance.host = inet_ntoa(sender_addr.sin_addr);
    instance.port = ntohs(*reinterpret_cast<uint16_t*>(&buffer[6]));
    
    // Parse the rest as simple key-value pairs (null-terminated strings)
    buffer[recv_result] = '\0';
    std::string payload(&buffer[8], recv_result - 8);
    
    // Simple parsing: id=xxx;version=xxx;status=xxx;uptime=xxx;topics=xxx;subs=xxx;peers=xxx
    std::istringstream stream(payload);
    std::string pair;
    while (std::getline(stream, pair, ';')) {
        auto eq_pos = pair.find('=');
        if (eq_pos == std::string::npos) continue;
        
        std::string key = pair.substr(0, eq_pos);
        std::string value = pair.substr(eq_pos + 1);
        
        if (key == "id") instance.instance_id = value;
        else if (key == "version") instance.version = value;
        else if (key == "status") instance.status = value;
        else if (key == "uptime") instance.uptime_seconds = std::stoll(value);
        else if (key == "topics") instance.topic_count = std::stoi(value);
        else if (key == "subs") instance.subscriber_count = std::stoi(value);
        else if (key == "peers") instance.peer_count = std::stoi(value);
    }
    
    instance.discovered_at = std::chrono::steady_clock::now();
    return true;
}

std::vector<DiscoveredInstance> Discovery::discover() {
    return discover(config_.timeout);
}

std::vector<DiscoveredInstance> Discovery::discover(std::chrono::milliseconds timeout) {
    cached_instances_.clear();
    
    try {
        init_socket();
    } catch (const std::exception&) {
        return cached_instances_;
    }
    
    send_discovery_request();
    
    auto start = std::chrono::steady_clock::now();
    auto deadline = start + timeout;
    
    while (cached_instances_.size() < static_cast<size_t>(config_.max_instances)) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) break;
        
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        
        DiscoveredInstance instance;
        if (receive_response(instance, remaining)) {
            // Check for duplicates
            bool duplicate = false;
            for (const auto& existing : cached_instances_) {
                if (existing.host == instance.host && existing.port == instance.port) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                cached_instances_.push_back(std::move(instance));
            }
        }
    }
    
    // Sort by port for consistent ordering
    std::sort(cached_instances_.begin(), cached_instances_.end(),
              [](const DiscoveredInstance& a, const DiscoveredInstance& b) {
                  if (a.host != b.host) return a.host < b.host;
                  return a.port < b.port;
              });
    
    return cached_instances_;
}

const std::vector<DiscoveredInstance>& Discovery::get_cached() const {
    return cached_instances_;
}

const DiscoveredInstance* Discovery::get_by_index(int index) const {
    if (index < 1 || index > static_cast<int>(cached_instances_.size())) {
        return nullptr;
    }
    return &cached_instances_[index - 1];
}

const DiscoveredInstance* Discovery::get_by_port(uint16_t port) const {
    for (const auto& instance : cached_instances_) {
        if (instance.port == port) {
            return &instance;
        }
    }
    return nullptr;
}

void Discovery::clear_cache() {
    cached_instances_.clear();
}

} // namespace nexusd::cli
