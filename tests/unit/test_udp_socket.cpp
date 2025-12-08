/**
 * @file test_udp_socket.cpp
 * @brief Unit tests for UDP socket abstraction
 */

#include <gtest/gtest.h>
#include <nexusd/net/udp_socket.hpp>

#include <thread>
#include <chrono>
#include <atomic>

using namespace nexusd::net;

class UDPSocketTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Platform-specific socket init is handled by UDPSocket constructor
    }
};

TEST_F(UDPSocketTest, CreateAndClose) {
    UDPSocket socket;
    EXPECT_FALSE(socket.isValid());
    
    bool created = socket.create();
    EXPECT_TRUE(created);
    EXPECT_TRUE(socket.isValid());
    
    socket.close();
    EXPECT_FALSE(socket.isValid());
}

TEST_F(UDPSocketTest, BindToPort) {
    UDPSocket socket;
    ASSERT_TRUE(socket.create());
    
    // Bind to any available port
    bool bound = socket.bind(0);
    EXPECT_TRUE(bound);
    
    socket.close();
}

TEST_F(UDPSocketTest, SendReceiveLoopback) {
    UDPSocket sender;
    UDPSocket receiver;
    
    ASSERT_TRUE(sender.create());
    ASSERT_TRUE(receiver.create());
    
    // Bind receiver to a specific port
    uint16_t port = 54321;
    bool bound = receiver.bind(port);
    
    // If port is in use, try another
    if (!bound) {
        port = 54322;
        bound = receiver.bind(port);
    }
    
    if (!bound) {
        // Skip test if we can't bind
        GTEST_SKIP() << "Could not bind to test port";
    }
    
    // Set receive timeout
    receiver.setReceiveTimeout(1000);
    
    // Send data
    const char* message = "Hello, UDP!";
    ssize_t sent = sender.sendTo("127.0.0.1", port, message, std::strlen(message));
    EXPECT_GT(sent, 0);
    
    // Receive data
    char buffer[256];
    std::string from_addr;
    uint16_t from_port;
    ssize_t received = receiver.receiveFrom(buffer, sizeof(buffer), from_addr, from_port);
    
    EXPECT_GT(received, 0);
    if (received > 0) {
        buffer[received] = '\0';
        EXPECT_STREQ(buffer, message);
    }
    
    sender.close();
    receiver.close();
}

TEST_F(UDPSocketTest, ReceiveTimeout) {
    UDPSocket socket;
    ASSERT_TRUE(socket.create());
    ASSERT_TRUE(socket.bind(54323));
    
    // Set short timeout
    socket.setReceiveTimeout(100);
    
    // Try to receive (should timeout)
    char buffer[256];
    std::string from_addr;
    uint16_t from_port;
    
    auto start = std::chrono::steady_clock::now();
    ssize_t received = socket.receiveFrom(buffer, sizeof(buffer), from_addr, from_port);
    auto end = std::chrono::steady_clock::now();
    
    // Should return 0 or -1 on timeout
    EXPECT_LE(received, 0);
    
    // Should have waited at least 100ms
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    EXPECT_GE(elapsed.count(), 90); // Allow some tolerance
    
    socket.close();
}

TEST_F(UDPSocketTest, EnableBroadcast) {
    UDPSocket socket;
    ASSERT_TRUE(socket.create());
    
    bool enabled = socket.enableBroadcast();
    EXPECT_TRUE(enabled);
    
    socket.close();
}

// Multicast test (may not work in all environments)
TEST_F(UDPSocketTest, JoinMulticastGroup) {
    UDPSocket socket;
    ASSERT_TRUE(socket.create());
    ASSERT_TRUE(socket.bind(54324));
    
    // Try to join multicast group
    bool joined = socket.joinMulticastGroup("239.255.42.1");
    
    // This may fail if multicast is not supported in the test environment
    // Just ensure it doesn't crash
    (void)joined;
    
    socket.close();
}
