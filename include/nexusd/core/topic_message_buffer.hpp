/**
 * @file topic_message_buffer.hpp
 * @brief Ring buffer for topic messages to support gap recovery after reconnection
 * 
 * This buffer stores recent messages per topic to enable:
 * - Gap detection via sequence numbers
 * - Message replay for clients reconnecting after a brief disconnection
 * - Retained message delivery for new/resumed subscriptions
 */

#pragma once

#include <nexusd/core/export.hpp>

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace nexusd::core {

/**
 * @brief A buffered message with metadata for replay
 */
struct BufferedMessage {
    uint64_t sequence_number;     ///< Monotonically increasing per-topic sequence
    std::string topic;            ///< Topic name
    std::string payload;          ///< Message payload
    std::chrono::steady_clock::time_point timestamp;  ///< When message was received
};

/**
 * @brief Per-topic ring buffer with sequence tracking
 */
class TopicBuffer {
public:
    explicit TopicBuffer(uint32_t max_size)
        : max_size_(max_size), next_sequence_(1) {}

    /**
     * @brief Add a message to the buffer
     * @param payload The message payload
     * @param topic The topic name
     * @return The assigned sequence number
     */
    uint64_t push(const std::string& topic, const std::string& payload) {
        uint64_t seq = next_sequence_++;
        
        if (messages_.size() >= max_size_) {
            total_bytes_ -= messages_.front().payload.size();
            messages_.pop_front();
        }
        
        messages_.push_back(BufferedMessage{
            seq,
            topic,
            payload,
            std::chrono::steady_clock::now()
        });
        total_bytes_ += payload.size();
        
        return seq;
    }

    /**
     * @brief Get messages after a specific sequence number
     * @param after_sequence The last sequence the client received (0 = all messages)
     * @return Vector of messages with sequence > after_sequence
     */
    std::vector<BufferedMessage> getMessagesAfter(uint64_t after_sequence) const {
        std::vector<BufferedMessage> result;
        for (const auto& msg : messages_) {
            if (msg.sequence_number > after_sequence) {
                result.push_back(msg);
            }
        }
        return result;
    }

    /**
     * @brief Get the most recent message (retained message)
     * @return The last message if buffer is not empty
     */
    std::optional<BufferedMessage> getLatest() const {
        if (messages_.empty()) {
            return std::nullopt;
        }
        return messages_.back();
    }

    /**
     * @brief Get the current sequence number (last assigned)
     */
    uint64_t currentSequence() const {
        return next_sequence_ - 1;
    }

    /**
     * @brief Get the oldest available sequence number
     * @return 0 if buffer is empty
     */
    uint64_t oldestSequence() const {
        if (messages_.empty()) {
            return 0;
        }
        return messages_.front().sequence_number;
    }

    /**
     * @brief Check if a sequence is available for replay
     */
    bool hasSequence(uint64_t seq) const {
        if (messages_.empty()) {
            return false;
        }
        return seq >= messages_.front().sequence_number && 
               seq <= messages_.back().sequence_number;
    }

    /**
     * @brief Get total bytes used by this buffer
     */
    size_t totalBytes() const { return total_bytes_; }

    /**
     * @brief Get number of messages in buffer
     */
    size_t size() const { return messages_.size(); }

    /**
     * @brief Clear all messages
     */
    void clear() {
        messages_.clear();
        total_bytes_ = 0;
    }

private:
    uint32_t max_size_;
    uint64_t next_sequence_;
    std::deque<BufferedMessage> messages_;
    size_t total_bytes_ = 0;
};

/**
 * @brief Thread-safe manager for all topic message buffers
 * 
 * Provides:
 * - Per-topic ring buffers with configurable size
 * - Global memory limit enforcement
 * - Gap detection and message replay
 * - Retained message support
 */
class NEXUSD_CORE_EXPORT TopicMessageBuffer {
public:
    /**
     * @brief Constructor
     * @param buffer_size_per_topic Max messages per topic buffer
     * @param max_total_memory_bytes Global memory limit (0 = unlimited)
     */
    explicit TopicMessageBuffer(
        uint32_t buffer_size_per_topic = 5,
        size_t max_total_memory_bytes = 52428800  // 50MB default
    );

    ~TopicMessageBuffer() = default;

    // Non-copyable
    TopicMessageBuffer(const TopicMessageBuffer&) = delete;
    TopicMessageBuffer& operator=(const TopicMessageBuffer&) = delete;

    /**
     * @brief Buffer a message for a topic
     * @param topic The topic name
     * @param payload The message payload
     * @return Assigned sequence number, or 0 if memory limit exceeded
     */
    uint64_t bufferMessage(const std::string& topic, const std::string& payload);

    /**
     * @brief Get messages for replay after a sequence number
     * @param topic The topic name
     * @param after_sequence Last sequence client received (0 = get retained only)
     * @param out_gap_detected Set to true if there's a gap (missed messages)
     * @param out_missed_count Number of messages that were missed and can't be replayed
     * @return Vector of messages to replay
     */
    std::vector<BufferedMessage> getMessagesForReplay(
        const std::string& topic,
        uint64_t after_sequence,
        bool& out_gap_detected,
        uint64_t& out_missed_count
    );

    /**
     * @brief Get the retained (latest) message for a topic
     * @param topic The topic name
     * @return The latest message if available
     */
    std::optional<BufferedMessage> getRetainedMessage(const std::string& topic);

    /**
     * @brief Get the current sequence number for a topic
     * @param topic The topic name
     * @return Current sequence number, or 0 if topic has no messages
     */
    uint64_t getCurrentSequence(const std::string& topic);

    /**
     * @brief Check if sequence is available for replay
     * @param topic The topic name
     * @param sequence The sequence number to check
     * @return true if the sequence can be replayed
     */
    bool isSequenceAvailable(const std::string& topic, uint64_t sequence);

    /**
     * @brief Get statistics
     */
    struct Stats {
        size_t topic_count;
        size_t total_messages;
        size_t total_bytes;
        size_t max_bytes;
    };
    Stats getStats() const;

    /**
     * @brief Clear all buffers
     */
    void clear();

    /**
     * @brief Clear buffer for a specific topic
     */
    void clearTopic(const std::string& topic);

private:
    /**
     * @brief Evict oldest messages to make room (LRU across topics)
     * @param needed_bytes Bytes needed for new message
     * @return true if space was freed
     */
    bool evictForSpace(size_t needed_bytes);

    mutable std::mutex mutex_;
    std::unordered_map<std::string, TopicBuffer> topic_buffers_;
    uint32_t buffer_size_per_topic_;
    size_t max_total_memory_bytes_;
    size_t current_total_bytes_ = 0;
};

}  // namespace nexusd::core
