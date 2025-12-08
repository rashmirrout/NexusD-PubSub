/**
 * @file udp_socket.hpp
 * @brief Cross-platform UDP socket with multicast support.
 *
 * Provides a RAII wrapper around UDP sockets with multicast group
 * join/leave, TTL configuration, and timeout-based receive.
 *
 * @copyright Copyright (c) 2024 NexusD Contributors
 * @license MIT License
 */

#pragma once

#include "nexusd/net/export.hpp"
#include "nexusd/net/platform.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace nexusd {
namespace net {

/**
 * @struct SocketAddress
 * @brief IP address and port pair.
 */
struct NEXUSD_NET_API SocketAddress {
    std::string ip;
    uint16_t port;
    
    SocketAddress() : ip("0.0.0.0"), port(0) {}
    SocketAddress(const std::string& ip_, uint16_t port_) : ip(ip_), port(port_) {}
    
    std::string toString() const { return ip + ":" + std::to_string(port); }
    
    bool operator==(const SocketAddress& other) const {
        return ip == other.ip && port == other.port;
    }
};

/**
 * @class UdpSocket
 * @brief RAII UDP socket wrapper with multicast support.
 *
 * Features:
 * - Cross-platform (Windows/Linux/macOS)
 * - Multicast group join/leave
 * - Configurable TTL
 * - Timeout-based receive using select()
 * - Non-blocking mode support
 *
 * Usage:
 * @code
 * UdpSocket sock;
 * sock.bind(5000);
 * sock.joinMulticastGroup("239.255.77.1");
 * sock.setMulticastTTL(1);
 * 
 * // Receive with timeout
 * std::vector<uint8_t> buffer(1500);
 * SocketAddress sender;
 * int received = sock.receiveFrom(buffer.data(), buffer.size(), 1000, sender);
 * 
 * // Send
 * sock.sendTo(SocketAddress("239.255.77.1", 5000), data.data(), data.size());
 * @endcode
 */
class NEXUSD_NET_API UdpSocket {
public:
    /**
     * @brief Create an unbound UDP socket.
     */
    UdpSocket();

    /**
     * @brief Destructor - closes the socket.
     */
    ~UdpSocket();

    // Non-copyable, but movable
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;

    /**
     * @brief Check if the socket is valid/open.
     */
    bool isValid() const { return socket_ != INVALID_SOCKET_HANDLE; }

    /**
     * @brief Get the underlying socket handle.
     */
    SocketHandle handle() const { return socket_; }

    /**
     * @brief Bind the socket to a local port.
     * @param port The port to bind to (0 for auto-assign).
     * @param address The local address to bind to (default: any).
     * @return True on success.
     */
    bool bind(uint16_t port, const std::string& address = "0.0.0.0");

    /**
     * @brief Get the local port the socket is bound to.
     */
    uint16_t getLocalPort() const;

    /**
     * @brief Enable address reuse (SO_REUSEADDR).
     * Call before bind().
     */
    bool setReuseAddress(bool enable);

    /**
     * @brief Enable broadcast sending.
     */
    bool setBroadcast(bool enable);

    /**
     * @brief Set the multicast TTL (time-to-live / hop limit).
     * @param ttl TTL value (1 = local subnet only).
     */
    bool setMulticastTTL(int ttl);

    /**
     * @brief Enable/disable multicast loopback.
     * When enabled, the sender also receives its own multicast packets.
     */
    bool setMulticastLoopback(bool enable);

    /**
     * @brief Set the outgoing interface for multicast packets.
     * @param interfaceAddress IP address of the local interface.
     */
    bool setMulticastInterface(const std::string& interfaceAddress);

    /**
     * @brief Join a multicast group.
     * @param groupAddress Multicast group IP (e.g., "239.255.77.1").
     * @param interfaceAddress Local interface IP (empty = default).
     * @return True on success.
     */
    bool joinMulticastGroup(const std::string& groupAddress,
                            const std::string& interfaceAddress = "");

    /**
     * @brief Leave a multicast group.
     */
    bool leaveMulticastGroup(const std::string& groupAddress,
                             const std::string& interfaceAddress = "");

    /**
     * @brief Send data to an address.
     * @param dest Destination address.
     * @param data Pointer to data buffer.
     * @param length Number of bytes to send.
     * @return Number of bytes sent, or -1 on error.
     */
    int sendTo(const SocketAddress& dest, const void* data, size_t length);

    /**
     * @brief Receive data with timeout.
     * @param buffer Buffer to receive into.
     * @param bufferSize Size of the buffer.
     * @param timeoutMs Timeout in milliseconds (0 = non-blocking, -1 = infinite).
     * @param sender Output: address of the sender.
     * @return Number of bytes received, 0 on timeout, -1 on error.
     */
    int receiveFrom(void* buffer, size_t bufferSize, int timeoutMs,
                    SocketAddress& sender);

    /**
     * @brief Close the socket.
     */
    void close();

    /**
     * @brief Get the last socket error code.
     */
    int getLastError() const { return lastError_; }

private:
    SocketHandle socket_;
    int lastError_;
    
    void setLastError();
};

}  // namespace net
}  // namespace nexusd
