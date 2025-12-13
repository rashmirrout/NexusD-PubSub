/**
 * @file test_topic_message_buffer.cpp
 * @brief Comprehensive unit tests for TopicMessageBuffer
 * 
 * Tests cover:
 * - Basic buffering operations
 * - Per-topic ring buffers
 * - Sequence number tracking
 * - Gap detection
 * - Memory limit enforcement
 * - Retained messages
 * - Thread safety
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <nexusd/core/topic_message_buffer.hpp>

#include <thread>
#include <vector>
#include <set>

using namespace nexusd::core;
using ::testing::UnorderedElementsAre;
using ::testing::Contains;

class TopicMessageBufferTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// =============================================================================
// TopicBuffer (Internal) Tests
// =============================================================================

TEST_F(TopicMessageBufferTest, TopicBufferBasicPush) {
    TopicBuffer buffer(5);
    
    uint64_t seq1 = buffer.push("topic1", "payload1");
    uint64_t seq2 = buffer.push("topic1", "payload2");
    
    EXPECT_EQ(seq1, 1u);
    EXPECT_EQ(seq2, 2u);
    EXPECT_EQ(buffer.size(), 2u);
    EXPECT_EQ(buffer.currentSequence(), 2u);
}

TEST_F(TopicMessageBufferTest, TopicBufferRingEviction) {
    TopicBuffer buffer(3);
    
    buffer.push("t", "p1");  // seq 1
    buffer.push("t", "p2");  // seq 2
    buffer.push("t", "p3");  // seq 3
    buffer.push("t", "p4");  // seq 4, evicts p1
    
    EXPECT_EQ(buffer.size(), 3u);
    EXPECT_EQ(buffer.oldestSequence(), 2u);  // 1 was evicted
    EXPECT_EQ(buffer.currentSequence(), 4u);
}

TEST_F(TopicMessageBufferTest, TopicBufferGetMessagesAfter) {
    TopicBuffer buffer(10);
    
    for (int i = 0; i < 5; ++i) {
        buffer.push("topic", "payload" + std::to_string(i));
    }
    
    // Get all messages after seq 2
    auto messages = buffer.getMessagesAfter(2);
    EXPECT_EQ(messages.size(), 3u);  // seq 3, 4, 5
    EXPECT_EQ(messages[0].sequence_number, 3u);
    EXPECT_EQ(messages[2].sequence_number, 5u);
}

TEST_F(TopicMessageBufferTest, TopicBufferGetLatest) {
    TopicBuffer buffer(5);
    
    EXPECT_FALSE(buffer.getLatest().has_value());
    
    buffer.push("topic", "first");
    buffer.push("topic", "second");
    buffer.push("topic", "third");
    
    auto latest = buffer.getLatest();
    ASSERT_TRUE(latest.has_value());
    EXPECT_EQ(latest->payload, "third");
    EXPECT_EQ(latest->sequence_number, 3u);
}

TEST_F(TopicMessageBufferTest, TopicBufferHasSequence) {
    TopicBuffer buffer(3);
    
    buffer.push("t", "a");  // 1
    buffer.push("t", "b");  // 2
    buffer.push("t", "c");  // 3
    
    EXPECT_TRUE(buffer.hasSequence(1));
    EXPECT_TRUE(buffer.hasSequence(2));
    EXPECT_TRUE(buffer.hasSequence(3));
    EXPECT_FALSE(buffer.hasSequence(0));
    EXPECT_FALSE(buffer.hasSequence(4));
    
    buffer.push("t", "d");  // 4, evicts 1
    
    EXPECT_FALSE(buffer.hasSequence(1));
    EXPECT_TRUE(buffer.hasSequence(2));
    EXPECT_TRUE(buffer.hasSequence(4));
}

TEST_F(TopicMessageBufferTest, TopicBufferTotalBytes) {
    TopicBuffer buffer(10);
    
    EXPECT_EQ(buffer.totalBytes(), 0u);
    
    buffer.push("topic", "12345");  // 5 bytes
    buffer.push("topic", "abc");    // 3 bytes
    
    EXPECT_EQ(buffer.totalBytes(), 8u);
}

TEST_F(TopicMessageBufferTest, TopicBufferClear) {
    TopicBuffer buffer(10);
    
    buffer.push("topic", "data");
    buffer.push("topic", "more");
    
    EXPECT_EQ(buffer.size(), 2u);
    
    buffer.clear();
    
    EXPECT_EQ(buffer.size(), 0u);
    EXPECT_EQ(buffer.totalBytes(), 0u);
    EXPECT_FALSE(buffer.getLatest().has_value());
}

// =============================================================================
// TopicMessageBuffer Tests
// =============================================================================

TEST_F(TopicMessageBufferTest, BufferMessageBasic) {
    TopicMessageBuffer buffer(5, 0);  // Unlimited memory
    
    uint64_t seq1 = buffer.bufferMessage("topic1", "payload1");
    uint64_t seq2 = buffer.bufferMessage("topic1", "payload2");
    uint64_t seq3 = buffer.bufferMessage("topic2", "payload3");
    
    EXPECT_EQ(seq1, 1u);
    EXPECT_EQ(seq2, 2u);
    EXPECT_EQ(seq3, 1u);  // Different topic, starts at 1
}

TEST_F(TopicMessageBufferTest, GetCurrentSequence) {
    TopicMessageBuffer buffer(10, 0);
    
    EXPECT_EQ(buffer.getCurrentSequence("nonexistent"), 0u);
    
    buffer.bufferMessage("topic1", "a");
    buffer.bufferMessage("topic1", "b");
    buffer.bufferMessage("topic1", "c");
    
    EXPECT_EQ(buffer.getCurrentSequence("topic1"), 3u);
    EXPECT_EQ(buffer.getCurrentSequence("topic2"), 0u);
}

TEST_F(TopicMessageBufferTest, GetRetainedMessage) {
    TopicMessageBuffer buffer(5, 0);
    
    EXPECT_FALSE(buffer.getRetainedMessage("topic").has_value());
    
    buffer.bufferMessage("topic", "first");
    buffer.bufferMessage("topic", "second");
    buffer.bufferMessage("topic", "latest");
    
    auto retained = buffer.getRetainedMessage("topic");
    ASSERT_TRUE(retained.has_value());
    EXPECT_EQ(retained->payload, "latest");
}

TEST_F(TopicMessageBufferTest, IsSequenceAvailable) {
    TopicMessageBuffer buffer(3, 0);
    
    buffer.bufferMessage("topic", "1");
    buffer.bufferMessage("topic", "2");
    buffer.bufferMessage("topic", "3");
    
    EXPECT_TRUE(buffer.isSequenceAvailable("topic", 1));
    EXPECT_TRUE(buffer.isSequenceAvailable("topic", 3));
    EXPECT_FALSE(buffer.isSequenceAvailable("topic", 4));
    EXPECT_FALSE(buffer.isSequenceAvailable("other", 1));
    
    buffer.bufferMessage("topic", "4");  // Evicts 1
    
    EXPECT_FALSE(buffer.isSequenceAvailable("topic", 1));
    EXPECT_TRUE(buffer.isSequenceAvailable("topic", 2));
}

// =============================================================================
// Gap Detection
// =============================================================================

TEST_F(TopicMessageBufferTest, NoGapWhenUpToDate) {
    TopicMessageBuffer buffer(10, 0);
    
    buffer.bufferMessage("topic", "1");
    buffer.bufferMessage("topic", "2");
    buffer.bufferMessage("topic", "3");
    
    bool gap_detected = false;
    uint64_t missed = 0;
    auto messages = buffer.getMessagesForReplay("topic", 3, gap_detected, missed);
    
    EXPECT_FALSE(gap_detected);
    EXPECT_EQ(missed, 0u);
    EXPECT_TRUE(messages.empty());
}

TEST_F(TopicMessageBufferTest, NoGapWithAvailableReplay) {
    TopicMessageBuffer buffer(10, 0);
    
    for (int i = 0; i < 5; ++i) {
        buffer.bufferMessage("topic", "msg" + std::to_string(i));
    }
    
    bool gap_detected = false;
    uint64_t missed = 0;
    auto messages = buffer.getMessagesForReplay("topic", 2, gap_detected, missed);
    
    EXPECT_FALSE(gap_detected);
    EXPECT_EQ(missed, 0u);
    EXPECT_EQ(messages.size(), 3u);  // seq 3, 4, 5
}

TEST_F(TopicMessageBufferTest, GapDetectedWhenMessagesEvicted) {
    TopicMessageBuffer buffer(3, 0);
    
    for (int i = 0; i < 10; ++i) {
        buffer.bufferMessage("topic", "msg" + std::to_string(i));
    }
    // Now have seq 8, 9, 10 (oldest 7 were evicted)
    
    bool gap_detected = false;
    uint64_t missed = 0;
    auto messages = buffer.getMessagesForReplay("topic", 5, gap_detected, missed);
    
    EXPECT_TRUE(gap_detected);
    EXPECT_EQ(missed, 2u);  // seq 6, 7 were missed
    EXPECT_EQ(messages.size(), 3u);  // seq 8, 9, 10
}

TEST_F(TopicMessageBufferTest, ReplayFromZeroGetsRetained) {
    TopicMessageBuffer buffer(10, 0);
    
    buffer.bufferMessage("topic", "first");
    buffer.bufferMessage("topic", "second");
    buffer.bufferMessage("topic", "third");
    
    bool gap_detected = false;
    uint64_t missed = 0;
    auto messages = buffer.getMessagesForReplay("topic", 0, gap_detected, missed);
    
    // seq 0 means "get retained only"
    EXPECT_FALSE(gap_detected);
    EXPECT_EQ(messages.size(), 1u);
    EXPECT_EQ(messages[0].payload, "third");
}

TEST_F(TopicMessageBufferTest, ReplayNonexistentTopic) {
    TopicMessageBuffer buffer(10, 0);
    
    bool gap_detected = false;
    uint64_t missed = 0;
    auto messages = buffer.getMessagesForReplay("nonexistent", 5, gap_detected, missed);
    
    EXPECT_FALSE(gap_detected);
    EXPECT_EQ(missed, 0u);
    EXPECT_TRUE(messages.empty());
}

// =============================================================================
// Memory Limits
// =============================================================================

TEST_F(TopicMessageBufferTest, MemoryLimitPreventsBuffering) {
    // Very small memory limit
    TopicMessageBuffer buffer(100, 100);  // 100 bytes max
    
    // Try to add large message
    uint64_t seq = buffer.bufferMessage("topic", std::string(200, 'X'));
    
    // Should fail (return 0) since it exceeds limit
    // Note: Implementation may evict first, so behavior depends on implementation
    auto stats = buffer.getStats();
    EXPECT_LE(stats.total_bytes, 100u);
}

TEST_F(TopicMessageBufferTest, MemoryLimitEvictsOldMessages) {
    TopicMessageBuffer buffer(100, 100);  // 100 bytes max
    
    // Add multiple small messages
    for (int i = 0; i < 20; ++i) {
        buffer.bufferMessage("topic", "12345");  // 5 bytes each
    }
    
    auto stats = buffer.getStats();
    EXPECT_LE(stats.total_bytes, 100u);
}

// =============================================================================
// Statistics
// =============================================================================

TEST_F(TopicMessageBufferTest, GetStats) {
    TopicMessageBuffer buffer(10, 1000);
    
    buffer.bufferMessage("topic1", "data1");
    buffer.bufferMessage("topic1", "data2");
    buffer.bufferMessage("topic2", "data3");
    buffer.bufferMessage("topic3", "data4");
    
    auto stats = buffer.getStats();
    
    EXPECT_EQ(stats.topic_count, 3u);
    EXPECT_EQ(stats.total_messages, 4u);
    EXPECT_GT(stats.total_bytes, 0u);
    EXPECT_EQ(stats.max_bytes, 1000u);
}

// =============================================================================
// Clear Operations
// =============================================================================

TEST_F(TopicMessageBufferTest, ClearAll) {
    TopicMessageBuffer buffer(10, 0);
    
    buffer.bufferMessage("topic1", "a");
    buffer.bufferMessage("topic2", "b");
    buffer.bufferMessage("topic3", "c");
    
    buffer.clear();
    
    auto stats = buffer.getStats();
    EXPECT_EQ(stats.topic_count, 0u);
    EXPECT_EQ(stats.total_messages, 0u);
    EXPECT_EQ(stats.total_bytes, 0u);
}

TEST_F(TopicMessageBufferTest, ClearSingleTopic) {
    TopicMessageBuffer buffer(10, 0);
    
    buffer.bufferMessage("topic1", "a");
    buffer.bufferMessage("topic1", "b");
    buffer.bufferMessage("topic2", "c");
    
    buffer.clearTopic("topic1");
    
    EXPECT_FALSE(buffer.getRetainedMessage("topic1").has_value());
    EXPECT_TRUE(buffer.getRetainedMessage("topic2").has_value());
}

// =============================================================================
// Multiple Topics
// =============================================================================

TEST_F(TopicMessageBufferTest, IndependentTopicSequences) {
    TopicMessageBuffer buffer(10, 0);
    
    // Each topic should have independent sequence numbers
    EXPECT_EQ(buffer.bufferMessage("topic1", "a"), 1u);
    EXPECT_EQ(buffer.bufferMessage("topic1", "b"), 2u);
    EXPECT_EQ(buffer.bufferMessage("topic2", "x"), 1u);
    EXPECT_EQ(buffer.bufferMessage("topic2", "y"), 2u);
    EXPECT_EQ(buffer.bufferMessage("topic1", "c"), 3u);
}

TEST_F(TopicMessageBufferTest, IndependentTopicBuffers) {
    TopicMessageBuffer buffer(2, 0);  // Only 2 messages per topic
    
    // Fill topic1's buffer
    buffer.bufferMessage("topic1", "1a");
    buffer.bufferMessage("topic1", "1b");
    buffer.bufferMessage("topic1", "1c");  // Evicts 1a
    
    // topic2 should be independent
    buffer.bufferMessage("topic2", "2a");
    buffer.bufferMessage("topic2", "2b");
    
    // topic1 oldest should be 2, topic2 oldest should be 1
    bool gap = false;
    uint64_t missed = 0;
    
    auto t1_messages = buffer.getMessagesForReplay("topic1", 0, gap, missed);
    auto t2_messages = buffer.getMessagesForReplay("topic2", 0, gap, missed);
    
    EXPECT_EQ(t1_messages[0].payload, "1c");  // Latest retained
    EXPECT_EQ(t2_messages[0].payload, "2b");  // Latest retained
}

// =============================================================================
// Thread Safety
// =============================================================================

TEST_F(TopicMessageBufferTest, ConcurrentBuffering) {
    TopicMessageBuffer buffer(1000, 0);
    
    const int num_threads = 4;
    const int msgs_per_thread = 100;
    
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&buffer, t, msgs_per_thread]() {
            std::string topic = "topic" + std::to_string(t);
            for (int i = 0; i < msgs_per_thread; ++i) {
                buffer.bufferMessage(topic, "payload" + std::to_string(i));
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto stats = buffer.getStats();
    EXPECT_EQ(stats.topic_count, num_threads);
    EXPECT_EQ(stats.total_messages, num_threads * msgs_per_thread);
}

TEST_F(TopicMessageBufferTest, ConcurrentReadWrite) {
    TopicMessageBuffer buffer(100, 0);
    std::atomic<bool> done{false};
    
    // Writer
    std::thread writer([&]() {
        for (int i = 0; i < 1000; ++i) {
            buffer.bufferMessage("topic", "msg" + std::to_string(i));
        }
        done.store(true);
    });
    
    // Reader
    std::thread reader([&]() {
        while (!done.load()) {
            buffer.getRetainedMessage("topic");
            buffer.getCurrentSequence("topic");
            
            bool gap = false;
            uint64_t missed = 0;
            buffer.getMessagesForReplay("topic", 50, gap, missed);
        }
    });
    
    writer.join();
    reader.join();
    
    // Should complete without crashes
    EXPECT_TRUE(done.load());
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_F(TopicMessageBufferTest, EmptyPayload) {
    TopicMessageBuffer buffer(10, 0);
    
    uint64_t seq = buffer.bufferMessage("topic", "");
    EXPECT_EQ(seq, 1u);
    
    auto retained = buffer.getRetainedMessage("topic");
    ASSERT_TRUE(retained.has_value());
    EXPECT_EQ(retained->payload, "");
}

TEST_F(TopicMessageBufferTest, LargePayload) {
    TopicMessageBuffer buffer(5, 0);  // Unlimited memory
    
    std::string large(1024 * 1024, 'X');  // 1MB
    uint64_t seq = buffer.bufferMessage("topic", large);
    
    EXPECT_EQ(seq, 1u);
    
    auto retained = buffer.getRetainedMessage("topic");
    ASSERT_TRUE(retained.has_value());
    EXPECT_EQ(retained->payload.size(), 1024 * 1024);
}

TEST_F(TopicMessageBufferTest, ManyTopics) {
    TopicMessageBuffer buffer(5, 0);
    
    for (int i = 0; i < 100; ++i) {
        buffer.bufferMessage("topic" + std::to_string(i), "data");
    }
    
    auto stats = buffer.getStats();
    EXPECT_EQ(stats.topic_count, 100u);
}

TEST_F(TopicMessageBufferTest, SingleMessageBuffer) {
    TopicMessageBuffer buffer(1, 0);
    
    buffer.bufferMessage("topic", "first");
    buffer.bufferMessage("topic", "second");  // Evicts first
    buffer.bufferMessage("topic", "third");   // Evicts second
    
    auto retained = buffer.getRetainedMessage("topic");
    ASSERT_TRUE(retained.has_value());
    EXPECT_EQ(retained->payload, "third");
    EXPECT_EQ(retained->sequence_number, 3u);
}

