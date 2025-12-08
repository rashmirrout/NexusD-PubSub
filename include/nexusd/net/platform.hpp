/**
 * @file platform.hpp
 * @brief Cross-platform socket type definitions and includes.
 *
 * Abstracts Windows Winsock2 and POSIX socket APIs into a common interface.
 *
 * @copyright Copyright (c) 2024 NexusD Contributors
 * @license MIT License
 */

#pragma once

#ifdef _WIN32
    // Windows
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <WinSock2.h>
    #include <WS2tcpip.h>
    #include <iphlpapi.h>
    
    // Link with Winsock library
    #pragma comment(lib, "Ws2_32.lib")
    
    namespace nexusd {
    namespace net {
        using SocketHandle = SOCKET;
        constexpr SocketHandle INVALID_SOCKET_HANDLE = INVALID_SOCKET;
        
        inline int getLastSocketError() { return WSAGetLastError(); }
        inline void closeSocket(SocketHandle s) { ::closesocket(s); }
        
        // Initialize Winsock (call once at startup)
        inline bool initializeSockets() {
            WSADATA wsaData;
            return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
        }
        
        // Cleanup Winsock (call once at shutdown)
        inline void cleanupSockets() {
            WSACleanup();
        }
    }  // namespace net
    }  // namespace nexusd
    
#else
    // POSIX (Linux, macOS, etc.)
    #include <arpa/inet.h>
    #include <errno.h>
    #include <fcntl.h>
    #include <netinet/in.h>
    #include <sys/select.h>
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <unistd.h>
    
    namespace nexusd {
    namespace net {
        using SocketHandle = int;
        constexpr SocketHandle INVALID_SOCKET_HANDLE = -1;
        
        inline int getLastSocketError() { return errno; }
        inline void closeSocket(SocketHandle s) { ::close(s); }
        
        // No initialization needed on POSIX
        inline bool initializeSockets() { return true; }
        inline void cleanupSockets() {}
    }  // namespace net
    }  // namespace nexusd
    
#endif

namespace nexusd {
namespace net {

/**
 * @brief RAII helper for socket initialization.
 * 
 * Create one instance at program startup to ensure
 * proper Winsock initialization on Windows.
 */
class SocketInitializer {
public:
    SocketInitializer() : initialized_(initializeSockets()) {}
    ~SocketInitializer() { if (initialized_) cleanupSockets(); }
    
    bool isInitialized() const { return initialized_; }
    
    // Non-copyable
    SocketInitializer(const SocketInitializer&) = delete;
    SocketInitializer& operator=(const SocketInitializer&) = delete;
    
private:
    bool initialized_;
};

}  // namespace net
}  // namespace nexusd
