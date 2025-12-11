/**
 * @file topic_message_buffer.cpp
 * @brief Implementation of TopicMessageBuffer for gap recovery
 */

#include <nexusd/core/topic_message_buffer.hpp>
#include <nexusd/utils/logger.hpp>

#include <algorithm>

namespace nexusd::core {

TopicMessageBuffer::TopicMessageBuffer(
    uint32_t buffer_size_per_topic,
    size_t max_total_memory_bytes
)
    : buffer_size_per_topic_(buffer_size_per_topic)
    , max_total_memory_bytes_(max_total_memory_bytes)
{
    LOG_DEBUG("TopicMessageBuffer initialized: buffer_size={}, max_memory={}",
              buffer_size_per_topic_, max_total_memory_bytes_);
}

uint64_t TopicMessageBuffer::bufferMessage(
    const std::string& topic, 
    const std::string& payload
) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check memory limit
    if (max_total_memory_bytes_ > 0 && 
        current_total_bytes_ + payload.size() > max_total_memory_bytes_) {
        if (!evictForSpace(payload.size())) {
            LOG_WARN("TopicMessageBuffer: Cannot buffer message for '{}', "
                     "memory limit exceeded ({} bytes)", 
                     topic, max_total_memory_bytes_);
            return 0;
        }
    }
    
    // Get or create topic buffer
    auto it = topic_buffers_.find(topic);
    if (it == topic_buffers_.end()) {
        auto [inserted_it, success] = topic_buffers_.emplace(
            topic, TopicBuffer(buffer_size_per_topic_)
        );
        it = inserted_it;
        LOG_DEBUG("TopicMessageBuffer: Created buffer for topic '{}'", topic);
    }
    
    // Track bytes for messages being evicted from this topic's ring buffer
    size_t old_bytes = it->second.totalBytes();
    
    uint64_t seq = it->second.push(topic, payload);
    
    // Update global byte count
    size_t new_bytes = it->second.totalBytes();
    current_total_bytes_ = current_total_bytes_ - old_bytes + new_bytes;
    
    LOG_TRACE("TopicMessageBuffer: Buffered message for '{}' seq={} size={}", 
              topic, seq, payload.size());
    
    return seq;
}

std::vector<BufferedMessage> TopicMessageBuffer::getMessagesForReplay(
    const std::string& topic,
    uint64_t after_sequence,
    bool& out_gap_detected,
    uint64_t& out_missed_count
) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    out_gap_detected = false;
    out_missed_count = 0;
    
    auto it = topic_buffers_.find(topic);
    if (it == topic_buffers_.end()) {
        // No buffer for this topic - can't detect gaps without history
        return {};
    }
    
    const auto& buffer = it->second;
    
    if (after_sequence == 0) {
        // Client wants retained only, return latest message
        auto latest = buffer.getLatest();
        if (latest) {
            return {*latest};
        }
        return {};
    }
    
    uint64_t oldest_available = buffer.oldestSequence();
    uint64_t current = buffer.currentSequence();
    
    if (after_sequence >= current) {
        // Client is up to date
        return {};
    }
    
    if (after_sequence < oldest_available) {
        // Gap detected - some messages are no longer available
        out_gap_detected = true;
        out_missed_count = oldest_available - after_sequence - 1;
        
        LOG_DEBUG("TopicMessageBuffer: Gap detected for '{}': "
                  "client at seq {}, oldest available {}, missed {} messages",
                  topic, after_sequence, oldest_available, out_missed_count);
    }
    
    // Return all available messages after the client's last sequence
    return buffer.getMessagesAfter(after_sequence);
}

std::optional<BufferedMessage> TopicMessageBuffer::getRetainedMessage(
    const std::string& topic
) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = topic_buffers_.find(topic);
    if (it == topic_buffers_.end()) {
        return std::nullopt;
    }
    
    return it->second.getLatest();
}

uint64_t TopicMessageBuffer::getCurrentSequence(const std::string& topic) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = topic_buffers_.find(topic);
    if (it == topic_buffers_.end()) {
        return 0;
    }
    
    return it->second.currentSequence();
}

bool TopicMessageBuffer::isSequenceAvailable(
    const std::string& topic, 
    uint64_t sequence
) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = topic_buffers_.find(topic);
    if (it == topic_buffers_.end()) {
        return false;
    }
    
    return it->second.hasSequence(sequence);
}

TopicMessageBuffer::Stats TopicMessageBuffer::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    Stats stats{};
    stats.topic_count = topic_buffers_.size();
    stats.total_bytes = current_total_bytes_;
    stats.max_bytes = max_total_memory_bytes_;
    
    for (const auto& [topic, buffer] : topic_buffers_) {
        stats.total_messages += buffer.size();
    }
    
    return stats;
}

void TopicMessageBuffer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    topic_buffers_.clear();
    current_total_bytes_ = 0;
    
    LOG_DEBUG("TopicMessageBuffer: Cleared all buffers");
}

void TopicMessageBuffer::clearTopic(const std::string& topic) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = topic_buffers_.find(topic);
    if (it != topic_buffers_.end()) {
        current_total_bytes_ -= it->second.totalBytes();
        topic_buffers_.erase(it);
        LOG_DEBUG("TopicMessageBuffer: Cleared buffer for topic '{}'", topic);
    }
}

bool TopicMessageBuffer::evictForSpace(size_t needed_bytes) {
    // Simple LRU strategy: evict oldest messages across all topics
    // until we have enough space
    
    if (max_total_memory_bytes_ == 0) {
        return true;  // No limit
    }
    
    size_t target = max_total_memory_bytes_ - needed_bytes;
    
    // Find topics with the oldest messages and evict them
    while (current_total_bytes_ > target && !topic_buffers_.empty()) {
        // Find topic with oldest message (by timestamp)
        std::string oldest_topic;
        std::chrono::steady_clock::time_point oldest_time = 
            std::chrono::steady_clock::time_point::max();
        
        for (const auto& [topic, buffer] : topic_buffers_) {
            if (buffer.size() > 0) {
                // For simplicity, just pick first non-empty topic
                // A more sophisticated approach would track actual timestamps
                oldest_topic = topic;
                break;
            }
        }
        
        if (oldest_topic.empty()) {
            return false;  // No more messages to evict
        }
        
        // Clear the oldest topic
        auto it = topic_buffers_.find(oldest_topic);
        if (it != topic_buffers_.end()) {
            current_total_bytes_ -= it->second.totalBytes();
            topic_buffers_.erase(it);
            LOG_DEBUG("TopicMessageBuffer: Evicted topic '{}' for space", oldest_topic);
        }
    }
    
    return current_total_bytes_ + needed_bytes <= max_total_memory_bytes_;
}

}  // namespace nexusd::core
