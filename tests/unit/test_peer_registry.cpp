/**
 * @file test_peer_registry.cpp
 * @brief Unit tests for peer registry
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <nexusd/core/peer_registry.hpp>

#include <thread>
#include <vector>

using namespace nexusd::core;
using ::testing::UnorderedElementsAre;
using ::testing::Contains;
using ::testing::IsEmpty;

class PeerRegistryTest : public ::testing::Test {
protected:
    PeerRegistry registry;
};

TEST_F(PeerRegistryTest, UpdateAndGetPeer) {
    PeerInfo peer;
    peer.instance_uuid = "node-1";
    peer.cluster_id = "test-cluster";
    peer.rpc_ip = "192.168.1.10";
    peer.rpc_port = 5671;
    peer.topic_state_hash = 12345;
    
    registry.updatePeer(peer);
    
    auto retrieved = registry.getPeer("node-1");
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->instance_uuid, "node-1");
    EXPECT_EQ(retrieved->rpc_ip, "192.168.1.10");
    EXPECT_EQ(retrieved->rpc_port, 5671);
}

TEST_F(PeerRegistryTest, GetNonExistentPeer) {
    auto peer = registry.getPeer("non-existent");
    EXPECT_FALSE(peer.has_value());
}

TEST_F(PeerRegistryTest, RemovePeer) {
    PeerInfo peer;
    peer.instance_uuid = "node-1";
    peer.cluster_id = "test-cluster";
    registry.updatePeer(peer);
    
    EXPECT_TRUE(registry.getPeer("node-1").has_value());
    
    registry.removePeer("node-1");
    
    EXPECT_FALSE(registry.getPeer("node-1").has_value());
}

TEST_F(PeerRegistryTest, GetAllPeers) {
    PeerInfo peer1;
    peer1.instance_uuid = "node-1";
    peer1.cluster_id = "cluster";
    
    PeerInfo peer2;
    peer2.instance_uuid = "node-2";
    peer2.cluster_id = "cluster";
    
    registry.updatePeer(peer1);
    registry.updatePeer(peer2);
    
    auto peers = registry.getAllPeers();
    EXPECT_EQ(peers.size(), 2u);
}

TEST_F(PeerRegistryTest, ReapStalePeers) {
    PeerInfo peer;
    peer.instance_uuid = "stale-node";
    peer.cluster_id = "cluster";
    peer.last_seen = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    
    registry.updatePeer(peer);
    EXPECT_TRUE(registry.getPeer("stale-node").has_value());
    
    // Reap with 5 second timeout
    registry.reapStalePeers(std::chrono::seconds(5));
    
    EXPECT_FALSE(registry.getPeer("stale-node").has_value());
}

TEST_F(PeerRegistryTest, TopicSubscription) {
    registry.addSubscription("topic1", "node-1");
    registry.addSubscription("topic1", "node-2");
    registry.addSubscription("topic2", "node-1");
    
    auto subscribers = registry.getSubscribers("topic1");
    EXPECT_THAT(subscribers, UnorderedElementsAre("node-1", "node-2"));
    
    subscribers = registry.getSubscribers("topic2");
    EXPECT_THAT(subscribers, Contains("node-1"));
    
    subscribers = registry.getSubscribers("topic3");
    EXPECT_THAT(subscribers, IsEmpty());
}

TEST_F(PeerRegistryTest, RemoveSubscription) {
    registry.addSubscription("topic1", "node-1");
    registry.addSubscription("topic1", "node-2");
    
    registry.removeSubscription("topic1", "node-1");
    
    auto subscribers = registry.getSubscribers("topic1");
    EXPECT_EQ(subscribers.size(), 1u);
    EXPECT_THAT(subscribers, Contains("node-2"));
}

TEST_F(PeerRegistryTest, GetNodeSubscriptions) {
    registry.addSubscription("topic1", "node-1");
    registry.addSubscription("topic2", "node-1");
    registry.addSubscription("topic3", "node-1");
    
    auto topics = registry.getNodeSubscriptions("node-1");
    EXPECT_THAT(topics, UnorderedElementsAre("topic1", "topic2", "topic3"));
}

TEST_F(PeerRegistryTest, RetainedMessages) {
    std::vector<uint8_t> payload = {0x01, 0x02, 0x03, 0x04};
    registry.setRetainedMessage("topic1", payload);
    
    auto retrieved = registry.getRetainedMessage("topic1");
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(*retrieved, payload);
    
    auto missing = registry.getRetainedMessage("topic2");
    EXPECT_FALSE(missing.has_value());
}

TEST_F(PeerRegistryTest, ClearRetainedMessage) {
    std::vector<uint8_t> payload = {0x01};
    registry.setRetainedMessage("topic1", payload);
    
    registry.clearRetainedMessage("topic1");
    
    EXPECT_FALSE(registry.getRetainedMessage("topic1").has_value());
}

TEST_F(PeerRegistryTest, ThreadSafety) {
    std::vector<std::thread> threads;
    const int num_threads = 10;
    const int ops_per_thread = 100;
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([this, i, ops_per_thread]() {
            for (int j = 0; j < ops_per_thread; ++j) {
                std::string node = "node-" + std::to_string(i);
                std::string topic = "topic-" + std::to_string(j % 10);
                
                PeerInfo peer;
                peer.instance_uuid = node;
                peer.cluster_id = "cluster";
                registry.updatePeer(peer);
                
                registry.addSubscription(topic, node);
                registry.getSubscribers(topic);
                registry.getPeer(node);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // If we get here without crash/deadlock, test passes
    SUCCEED();
}

TEST_F(PeerRegistryTest, ComputeTopicStateHash) {
    registry.addSubscription("topic1", "node-1");
    registry.addSubscription("topic2", "node-1");
    
    uint64_t hash1 = registry.computeTopicStateHash("node-1");
    
    registry.addSubscription("topic3", "node-1");
    
    uint64_t hash2 = registry.computeTopicStateHash("node-1");
    
    // Hash should change when subscriptions change
    EXPECT_NE(hash1, hash2);
}
