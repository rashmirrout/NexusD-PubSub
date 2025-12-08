/**
 * @file udp_socket.cpp
 * @brief Cross-platform UDP socket implementation.
 *
 * @copyright Copyright (c) 2024 NexusD Contributors
 * @license MIT License
 */

#include "nexusd/net/udp_socket.hpp"
#include "nexusd/utils/logger.hpp"

#include <cstring>

namespace nexusd {
namespace net {

UdpSocket::UdpSocket()
    : socket_(INVALID_SOCKET_HANDLE)
    , lastError_(0)
{
    socket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == INVALID_SOCKET_HANDLE) {
        setLastError();
        LOG_ERROR("UdpSocket", "Failed to create socket: error {}", lastError_);
    }
}

UdpSocket::~UdpSocket() {
    close();
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept
    : socket_(other.socket_)
    , lastError_(other.lastError_)
{
    other.socket_ = INVALID_SOCKET_HANDLE;
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
    if (this != &other) {
        close();
        socket_ = other.socket_;
        lastError_ = other.lastError_;
        other.socket_ = INVALID_SOCKET_HANDLE;
    }
    return *this;
}

bool UdpSocket::bind(uint16_t port, const std::string& address) {
    if (!isValid()) {
        return false;
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (address.empty() || address == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, address.c_str(), &addr.sin_addr) != 1) {
            LOG_ERROR("UdpSocket", "Invalid bind address: {}", address);
            return false;
        }
    }

    if (::bind(socket_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        setLastError();
        LOG_ERROR("UdpSocket", "Failed to bind to {}:{} - error {}", 
                  address, port, lastError_);
        return false;
    }

    LOG_DEBUG("UdpSocket", "Bound to {}:{}", address, port);
    return true;
}

uint16_t UdpSocket::getLocalPort() const {
    if (!isValid()) {
        return 0;
    }

    struct sockaddr_in addr{};
    socklen_t addrLen = sizeof(addr);
    
    if (getsockname(socket_, reinterpret_cast<struct sockaddr*>(&addr), &addrLen) != 0) {
        return 0;
    }

    return ntohs(addr.sin_port);
}

bool UdpSocket::setReuseAddress(bool enable) {
    if (!isValid()) {
        return false;
    }

    int optval = enable ? 1 : 0;
    
#ifdef _WIN32
    // Windows uses SO_REUSEADDR differently; also set SO_EXCLUSIVEADDRUSE for security
    if (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, 
                   reinterpret_cast<const char*>(&optval), sizeof(optval)) != 0) {
        setLastError();
        return false;
    }
#else
    // Unix: also set SO_REUSEPORT if available
    if (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) != 0) {
        setLastError();
        return false;
    }
    #ifdef SO_REUSEPORT
    setsockopt(socket_, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
    #endif
#endif

    return true;
}

bool UdpSocket::setBroadcast(bool enable) {
    if (!isValid()) {
        return false;
    }

    int optval = enable ? 1 : 0;
    
#ifdef _WIN32
    if (setsockopt(socket_, SOL_SOCKET, SO_BROADCAST,
                   reinterpret_cast<const char*>(&optval), sizeof(optval)) != 0) {
#else
    if (setsockopt(socket_, SOL_SOCKET, SO_BROADCAST, &optval, sizeof(optval)) != 0) {
#endif
        setLastError();
        return false;
    }

    return true;
}

bool UdpSocket::setMulticastTTL(int ttl) {
    if (!isValid()) {
        return false;
    }

    unsigned char ttlVal = static_cast<unsigned char>(ttl);
    
#ifdef _WIN32
    if (setsockopt(socket_, IPPROTO_IP, IP_MULTICAST_TTL,
                   reinterpret_cast<const char*>(&ttlVal), sizeof(ttlVal)) != 0) {
#else
    if (setsockopt(socket_, IPPROTO_IP, IP_MULTICAST_TTL, &ttlVal, sizeof(ttlVal)) != 0) {
#endif
        setLastError();
        return false;
    }

    return true;
}

bool UdpSocket::setMulticastLoopback(bool enable) {
    if (!isValid()) {
        return false;
    }

    unsigned char loop = enable ? 1 : 0;
    
#ifdef _WIN32
    if (setsockopt(socket_, IPPROTO_IP, IP_MULTICAST_LOOP,
                   reinterpret_cast<const char*>(&loop), sizeof(loop)) != 0) {
#else
    if (setsockopt(socket_, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)) != 0) {
#endif
        setLastError();
        return false;
    }

    return true;
}

bool UdpSocket::setMulticastInterface(const std::string& interfaceAddress) {
    if (!isValid()) {
        return false;
    }

    struct in_addr addr{};
    if (interfaceAddress.empty()) {
        addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, interfaceAddress.c_str(), &addr) != 1) {
            LOG_ERROR("UdpSocket", "Invalid interface address: {}", interfaceAddress);
            return false;
        }
    }

#ifdef _WIN32
    if (setsockopt(socket_, IPPROTO_IP, IP_MULTICAST_IF,
                   reinterpret_cast<const char*>(&addr), sizeof(addr)) != 0) {
#else
    if (setsockopt(socket_, IPPROTO_IP, IP_MULTICAST_IF, &addr, sizeof(addr)) != 0) {
#endif
        setLastError();
        return false;
    }

    return true;
}

bool UdpSocket::joinMulticastGroup(const std::string& groupAddress,
                                   const std::string& interfaceAddress) {
    if (!isValid()) {
        return false;
    }

    struct ip_mreq mreq{};
    
    if (inet_pton(AF_INET, groupAddress.c_str(), &mreq.imr_multiaddr) != 1) {
        LOG_ERROR("UdpSocket", "Invalid multicast group address: {}", groupAddress);
        return false;
    }

    if (interfaceAddress.empty()) {
        mreq.imr_interface.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, interfaceAddress.c_str(), &mreq.imr_interface) != 1) {
            LOG_ERROR("UdpSocket", "Invalid interface address: {}", interfaceAddress);
            return false;
        }
    }

#ifdef _WIN32
    if (setsockopt(socket_, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   reinterpret_cast<const char*>(&mreq), sizeof(mreq)) != 0) {
#else
    if (setsockopt(socket_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != 0) {
#endif
        setLastError();
        LOG_ERROR("UdpSocket", "Failed to join multicast group {}: error {}",
                  groupAddress, lastError_);
        return false;
    }

    LOG_INFO("UdpSocket", "Joined multicast group {}", groupAddress);
    return true;
}

bool UdpSocket::leaveMulticastGroup(const std::string& groupAddress,
                                    const std::string& interfaceAddress) {
    if (!isValid()) {
        return false;
    }

    struct ip_mreq mreq{};
    
    if (inet_pton(AF_INET, groupAddress.c_str(), &mreq.imr_multiaddr) != 1) {
        return false;
    }

    if (interfaceAddress.empty()) {
        mreq.imr_interface.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, interfaceAddress.c_str(), &mreq.imr_interface) != 1) {
            return false;
        }
    }

#ifdef _WIN32
    if (setsockopt(socket_, IPPROTO_IP, IP_DROP_MEMBERSHIP,
                   reinterpret_cast<const char*>(&mreq), sizeof(mreq)) != 0) {
#else
    if (setsockopt(socket_, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq)) != 0) {
#endif
        setLastError();
        return false;
    }

    LOG_INFO("UdpSocket", "Left multicast group {}", groupAddress);
    return true;
}

int UdpSocket::sendTo(const SocketAddress& dest, const void* data, size_t length) {
    if (!isValid()) {
        return -1;
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(dest.port);
    
    if (inet_pton(AF_INET, dest.ip.c_str(), &addr.sin_addr) != 1) {
        LOG_ERROR("UdpSocket", "Invalid destination address: {}", dest.ip);
        return -1;
    }

#ifdef _WIN32
    int result = ::sendto(socket_, 
                          static_cast<const char*>(data), 
                          static_cast<int>(length), 
                          0,
                          reinterpret_cast<struct sockaddr*>(&addr), 
                          sizeof(addr));
#else
    ssize_t result = ::sendto(socket_, 
                              data, 
                              length, 
                              0,
                              reinterpret_cast<struct sockaddr*>(&addr), 
                              sizeof(addr));
#endif

    if (result < 0) {
        setLastError();
        return -1;
    }

    return static_cast<int>(result);
}

int UdpSocket::receiveFrom(void* buffer, size_t bufferSize, int timeoutMs,
                           SocketAddress& sender) {
    if (!isValid()) {
        return -1;
    }

    // Use select() for timeout
    if (timeoutMs >= 0) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(socket_, &readSet);

        struct timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;

#ifdef _WIN32
        int selectResult = ::select(0, &readSet, nullptr, nullptr, &tv);
#else
        int selectResult = ::select(socket_ + 1, &readSet, nullptr, nullptr, &tv);
#endif

        if (selectResult < 0) {
            setLastError();
            return -1;
        }
        if (selectResult == 0) {
            // Timeout
            return 0;
        }
    }

    struct sockaddr_in addr{};
    socklen_t addrLen = sizeof(addr);

#ifdef _WIN32
    int result = ::recvfrom(socket_,
                            static_cast<char*>(buffer),
                            static_cast<int>(bufferSize),
                            0,
                            reinterpret_cast<struct sockaddr*>(&addr),
                            &addrLen);
#else
    ssize_t result = ::recvfrom(socket_,
                                buffer,
                                bufferSize,
                                0,
                                reinterpret_cast<struct sockaddr*>(&addr),
                                &addrLen);
#endif

    if (result < 0) {
        setLastError();
        return -1;
    }

    // Extract sender address
    char ipStr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ipStr, sizeof(ipStr));
    sender.ip = ipStr;
    sender.port = ntohs(addr.sin_port);

    return static_cast<int>(result);
}

void UdpSocket::close() {
    if (isValid()) {
        closeSocket(socket_);
        socket_ = INVALID_SOCKET_HANDLE;
    }
}

void UdpSocket::setLastError() {
    lastError_ = getLastSocketError();
}

}  // namespace net
}  // namespace nexusd
