/**
 * @file test_subscriber_queue.cpp
 * @brief Comprehensive unit tests for SubscriberQueue with backpressure and TTL
 * 
 * Tests cover:
 * - All backpressure policies (DROP_OLDEST, DROP_NEWEST, BLOCK)
 * - TTL enforcement (lazy and reaper)
 * - Queue statistics
 * - Thread safety
 * - Edge cases
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <nexusd/core/subscriber_queue.hpp>

#include <thread>
#include <vector>
#include <chrono>
#include <string>

using namespace nexusd::core;
using namespace std::chrono_literals;

// Simple test message type
struct TestMessage {
    std::string id;
    std::string payload;
    
    TestMessage() = default;
    TestMessage(std::string id_, std::string payload_)
        : id(std::move(id_)), payload(std::move(payload_)) {}
};

class SubscriberQueueTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// =============================================================================
// Basic Operations
// =============================================================================

TEST_F(SubscriberQueueTest, DefaultConstruction) {
    SubscriberQueue<TestMessage> queue;
    
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0u);
    EXPECT_FALSE(queue.isClosed());
}

TEST_F(SubscriberQueueTest, CustomConstruction) {
    SubscriberQueue<TestMessage> queue(100, BackpressurePolicy::DROP_NEWEST, 1000, 5000);
    
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0u);
}

TEST_F(SubscriberQueueTest, EnqueueAndDequeue) {
    SubscriberQueue<TestMessage> queue;
    
    TestMessage msg("1", "hello");
    auto result = queue.enqueue(std::move(msg));
    
    EXPECT_EQ(result, EnqueueResult::ENQUEUED);
    EXPECT_EQ(queue.size(), 1u);
    EXPECT_FALSE(queue.empty());
    
    auto dequeued = queue.dequeue();
    ASSERT_TRUE(dequeued.has_value());
    EXPECT_EQ(dequeued->id, "1");
    EXPECT_EQ(dequeued->payload, "hello");
    EXPECT_TRUE(queue.empty());
}

TEST_F(SubscriberQueueTest, DequeueEmpty) {
    SubscriberQueue<TestMessage> queue;
    
    auto result = queue.dequeue();
    EXPECT_FALSE(result.has_value());
}

TEST_F(SubscriberQueueTest, MultipleEnqueueDequeue) {
    SubscriberQueue<TestMessage> queue;
    
    for (int i = 0; i < 10; ++i) {
        queue.enqueue(TestMessage(std::to_string(i), "payload"));
    }
    
    EXPECT_EQ(queue.size(), 10u);
    
    for (int i = 0; i < 10; ++i) {
        auto msg = queue.dequeue();
        ASSERT_TRUE(msg.has_value());
        EXPECT_EQ(msg->id, std::to_string(i));
    }
    
    EXPECT_TRUE(queue.empty());
}

// =============================================================================
// DROP_OLDEST Policy
// =============================================================================

TEST_F(SubscriberQueueTest, DropOldestWhenFull) {
    SubscriberQueue<TestMessage> queue(3, BackpressurePolicy::DROP_OLDEST);
    
    // Fill the queue
    queue.enqueue(TestMessage("1", "first"));
    queue.enqueue(TestMessage("2", "second"));
    queue.enqueue(TestMessage("3", "third"));
    EXPECT_EQ(queue.size(), 3u);
    
    // Add one more - should drop oldest
    auto result = queue.enqueue(TestMessage("4", "fourth"));
    EXPECT_EQ(result, EnqueueResult::ENQUEUED);
    EXPECT_EQ(queue.size(), 3u);
    
    // Verify oldest was dropped
    auto msg = queue.dequeue();
    EXPECT_EQ(msg->id, "2");  // "1" was dropped
}

TEST_F(SubscriberQueueTest, DropOldestStats) {
    SubscriberQueue<TestMessage> queue(2, BackpressurePolicy::DROP_OLDEST);
    
    queue.enqueue(TestMessage("1", "a"));
    queue.enqueue(TestMessage("2", "b"));
    queue.enqueue(TestMessage("3", "c"));  // Drops "1"
    queue.enqueue(TestMessage("4", "d"));  // Drops "2"
    
    auto stats = queue.getStats();
    EXPECT_EQ(stats.total_enqueued, 4u);
    EXPECT_EQ(stats.dropped_oldest, 2u);
    EXPECT_EQ(stats.current_depth, 2u);
}

// =============================================================================
// DROP_NEWEST Policy
// =============================================================================

TEST_F(SubscriberQueueTest, DropNewestWhenFull) {
    SubscriberQueue<TestMessage> queue(3, BackpressurePolicy::DROP_NEWEST);
    
    queue.enqueue(TestMessage("1", "first"));
    queue.enqueue(TestMessage("2", "second"));
    queue.enqueue(TestMessage("3", "third"));
    
    // Try to add when full - should be rejected
    auto result = queue.enqueue(TestMessage("4", "rejected"));
    EXPECT_EQ(result, EnqueueResult::DROPPED_NEWEST);
    EXPECT_EQ(queue.size(), 3u);
    
    // Original messages preserved
    auto msg = queue.dequeue();
    EXPECT_EQ(msg->id, "1");
}

TEST_F(SubscriberQueueTest, DropNewestStats) {
    SubscriberQueue<TestMessage> queue(2, BackpressurePolicy::DROP_NEWEST);
    
    queue.enqueue(TestMessage("1", "a"));
    queue.enqueue(TestMessage("2", "b"));
    queue.enqueue(TestMessage("3", "rejected1"));
    queue.enqueue(TestMessage("4", "rejected2"));
    
    auto stats = queue.getStats();
    EXPECT_EQ(stats.total_enqueued, 2u);
    EXPECT_EQ(stats.dropped_newest, 2u);
}

// =============================================================================
// BLOCK Policy
// =============================================================================

TEST_F(SubscriberQueueTest, BlockPolicyTimeout) {
    // Short timeout for testing
    SubscriberQueue<TestMessage> queue(2, BackpressurePolicy::BLOCK, 50, 0);
    
    queue.enqueue(TestMessage("1", "a"));
    queue.enqueue(TestMessage("2", "b"));
    
    // This should block, timeout, then drop oldest
    auto start = std::chrono::steady_clock::now();
    auto result = queue.enqueue(TestMessage("3", "c"));
    auto elapsed = std::chrono::steady_clock::now() - start;
    
    // Should have waited ~50ms
    EXPECT_GE(elapsed, 40ms);
    EXPECT_EQ(result, EnqueueResult::ENQUEUED);
    
    auto stats = queue.getStats();
    EXPECT_EQ(stats.block_timeouts, 1u);
    EXPECT_EQ(stats.dropped_oldest, 1u);
}

TEST_F(SubscriberQueueTest, BlockPolicyUnblockedByDequeue) {
    SubscriberQueue<TestMessage> queue(2, BackpressurePolicy::BLOCK, 5000, 0);
    
    queue.enqueue(TestMessage("1", "a"));
    queue.enqueue(TestMessage("2", "b"));
    
    std::atomic<bool> enqueue_done{false};
    
    // Start a thread that will try to enqueue when full
    std::thread producer([&]() {
        queue.enqueue(TestMessage("3", "c"));
        enqueue_done.store(true);
    });
    
    // Give producer time to block
    std::this_thread::sleep_for(50ms);
    EXPECT_FALSE(enqueue_done.load());
    
    // Dequeue to unblock
    auto msg = queue.dequeue();
    EXPECT_EQ(msg->id, "1");
    
    producer.join();
    EXPECT_TRUE(enqueue_done.load());
}

// =============================================================================
// TTL (Time To Live)
// =============================================================================

TEST_F(SubscriberQueueTest, TTLExpiredOnDequeue) {
    // 100ms default TTL
    SubscriberQueue<TestMessage> queue(100, BackpressurePolicy::DROP_OLDEST, 5000, 100);
    
    queue.enqueue(TestMessage("1", "expires"));
    
    // Wait for TTL to expire
    std::this_thread::sleep_for(150ms);
    
    // Message should be expired and skipped
    auto msg = queue.dequeue();
    EXPECT_FALSE(msg.has_value());
    
    auto stats = queue.getStats();
    EXPECT_EQ(stats.dropped_ttl, 1u);
}

TEST_F(SubscriberQueueTest, MessageLevelTTL) {
    // No default TTL
    SubscriberQueue<TestMessage> queue(100, BackpressurePolicy::DROP_OLDEST, 5000, 0);
    
    // Enqueue with explicit short TTL
    queue.enqueue(TestMessage("1", "short-ttl"), 100);
    queue.enqueue(TestMessage("2", "no-ttl"), 0);  // Infinite
    
    // Wait for short TTL to expire
    std::this_thread::sleep_for(150ms);
    
    // First message expired, second still valid
    auto msg = queue.dequeue();
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->id, "2");
}

TEST_F(SubscriberQueueTest, ReapExpired) {
    SubscriberQueue<TestMessage> queue(100, BackpressurePolicy::DROP_OLDEST, 5000, 50);
    
    // Add several messages
    for (int i = 0; i < 5; ++i) {
        queue.enqueue(TestMessage(std::to_string(i), "data"));
    }
    
    EXPECT_EQ(queue.size(), 5u);
    
    // Wait for TTL
    std::this_thread::sleep_for(100ms);
    
    // Reap expired
    size_t reaped = queue.reapExpired();
    EXPECT_EQ(reaped, 5u);
    EXPECT_TRUE(queue.empty());
}

TEST_F(SubscriberQueueTest, ReapPartialExpiry) {
    SubscriberQueue<TestMessage> queue(100, BackpressurePolicy::DROP_OLDEST, 5000, 0);
    
    // Short TTL messages
    queue.enqueue(TestMessage("1", "short"), 50);
    queue.enqueue(TestMessage("2", "short"), 50);
    
    // Wait for short TTL
    std::this_thread::sleep_for(100ms);
    
    // Long TTL messages
    queue.enqueue(TestMessage("3", "long"), 10000);
    queue.enqueue(TestMessage("4", "long"), 10000);
    
    // Reap should only remove expired ones
    size_t reaped = queue.reapExpired();
    EXPECT_EQ(reaped, 2u);
    EXPECT_EQ(queue.size(), 2u);
}

// =============================================================================
// Close/Disconnect
// =============================================================================

TEST_F(SubscriberQueueTest, EnqueueAfterClose) {
    SubscriberQueue<TestMessage> queue;
    
    queue.enqueue(TestMessage("1", "before"));
    queue.close();
    
    auto result = queue.enqueue(TestMessage("2", "after"));
    EXPECT_EQ(result, EnqueueResult::DROPPED_DISCONNECTED);
    
    EXPECT_TRUE(queue.isClosed());
}

TEST_F(SubscriberQueueTest, DequeueAfterClose) {
    SubscriberQueue<TestMessage> queue;
    
    queue.enqueue(TestMessage("1", "data"));
    queue.close();
    
    // Should still be able to drain existing messages
    auto msg = queue.dequeue();
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->id, "1");
}

// =============================================================================
// Statistics
// =============================================================================

TEST_F(SubscriberQueueTest, HighWatermark) {
    SubscriberQueue<TestMessage> queue(100);
    
    // Fill to 50
    for (int i = 0; i < 50; ++i) {
        queue.enqueue(TestMessage(std::to_string(i), "data"));
    }
    
    auto stats1 = queue.getStats();
    EXPECT_EQ(stats1.high_watermark, 50u);
    
    // Drain
    while (queue.dequeue()) {}
    
    // Add 30
    for (int i = 0; i < 30; ++i) {
        queue.enqueue(TestMessage(std::to_string(i), "data"));
    }
    
    // High watermark should still be 50
    auto stats2 = queue.getStats();
    EXPECT_EQ(stats2.high_watermark, 50u);
}

TEST_F(SubscriberQueueTest, DeliveredCount) {
    SubscriberQueue<TestMessage> queue;
    
    for (int i = 0; i < 10; ++i) {
        queue.enqueue(TestMessage(std::to_string(i), "data"));
    }
    
    for (int i = 0; i < 5; ++i) {
        queue.dequeue();
    }
    
    auto stats = queue.getStats();
    EXPECT_EQ(stats.total_enqueued, 10u);
    EXPECT_EQ(stats.total_delivered, 5u);
    EXPECT_EQ(stats.current_depth, 5u);
}

// =============================================================================
// Unlimited Queue
// =============================================================================

TEST_F(SubscriberQueueTest, UnlimitedQueue) {
    // limit = 0 means unlimited
    SubscriberQueue<TestMessage> queue(0);
    
    // Add many messages
    for (int i = 0; i < 1000; ++i) {
        auto result = queue.enqueue(TestMessage(std::to_string(i), "data"));
        EXPECT_EQ(result, EnqueueResult::ENQUEUED);
    }
    
    EXPECT_EQ(queue.size(), 1000u);
    
    auto stats = queue.getStats();
    EXPECT_EQ(stats.dropped_oldest, 0u);
    EXPECT_EQ(stats.dropped_newest, 0u);
}

// =============================================================================
// Thread Safety
// =============================================================================

TEST_F(SubscriberQueueTest, ConcurrentProducers) {
    SubscriberQueue<TestMessage> queue(10000);
    
    const int num_threads = 4;
    const int msgs_per_thread = 1000;
    
    std::vector<std::thread> producers;
    for (int t = 0; t < num_threads; ++t) {
        producers.emplace_back([&queue, t, msgs_per_thread]() {
            for (int i = 0; i < msgs_per_thread; ++i) {
                queue.enqueue(TestMessage(
                    std::to_string(t * msgs_per_thread + i),
                    "data"
                ));
            }
        });
    }
    
    for (auto& t : producers) {
        t.join();
    }
    
    EXPECT_EQ(queue.size(), num_threads * msgs_per_thread);
}

TEST_F(SubscriberQueueTest, ConcurrentProducerConsumer) {
    SubscriberQueue<TestMessage> queue(100);
    
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    std::atomic<bool> done{false};
    
    // Producer
    std::thread producer([&]() {
        for (int i = 0; i < 1000; ++i) {
            queue.enqueue(TestMessage(std::to_string(i), "data"));
            produced++;
        }
        done.store(true);
    });
    
    // Consumer
    std::thread consumer([&]() {
        while (!done.load() || !queue.empty()) {
            if (queue.dequeue()) {
                consumed++;
            }
        }
    });
    
    producer.join();
    consumer.join();
    
    EXPECT_EQ(produced.load(), 1000);
    // Some messages may have been dropped due to backpressure
    auto stats = queue.getStats();
    EXPECT_EQ(stats.total_enqueued + stats.dropped_oldest + stats.dropped_newest, 
              static_cast<uint64_t>(produced.load()));
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_F(SubscriberQueueTest, SingleElementQueue) {
    SubscriberQueue<TestMessage> queue(1, BackpressurePolicy::DROP_OLDEST);
    
    queue.enqueue(TestMessage("1", "a"));
    queue.enqueue(TestMessage("2", "b"));  // Drops "1"
    queue.enqueue(TestMessage("3", "c"));  // Drops "2"
    
    EXPECT_EQ(queue.size(), 1u);
    
    auto msg = queue.dequeue();
    EXPECT_EQ(msg->id, "3");
}

TEST_F(SubscriberQueueTest, EmptyMessage) {
    SubscriberQueue<TestMessage> queue;
    
    queue.enqueue(TestMessage("", ""));
    
    auto msg = queue.dequeue();
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->id, "");
    EXPECT_EQ(msg->payload, "");
}

TEST_F(SubscriberQueueTest, LargePayload) {
    SubscriberQueue<TestMessage> queue;
    
    std::string large_payload(1024 * 1024, 'X');  // 1MB
    queue.enqueue(TestMessage("large", large_payload));
    
    auto msg = queue.dequeue();
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->payload.size(), 1024 * 1024);
}

// =============================================================================
// EnqueueResult Enum Coverage
// =============================================================================

TEST_F(SubscriberQueueTest, AllEnqueueResults) {
    // ENQUEUED
    {
        SubscriberQueue<TestMessage> queue;
        auto result = queue.enqueue(TestMessage("1", "data"));
        EXPECT_EQ(result, EnqueueResult::ENQUEUED);
    }
    
    // DROPPED_NEWEST
    {
        SubscriberQueue<TestMessage> queue(1, BackpressurePolicy::DROP_NEWEST);
        queue.enqueue(TestMessage("1", "data"));
        auto result = queue.enqueue(TestMessage("2", "data"));
        EXPECT_EQ(result, EnqueueResult::DROPPED_NEWEST);
    }
    
    // DROPPED_DISCONNECTED
    {
        SubscriberQueue<TestMessage> queue;
        queue.close();
        auto result = queue.enqueue(TestMessage("1", "data"));
        EXPECT_EQ(result, EnqueueResult::DROPPED_DISCONNECTED);
    }
}

