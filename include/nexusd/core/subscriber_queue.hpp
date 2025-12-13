/**
 * @file subscriber_queue.hpp
 * @brief Per-subscriber message queue with backpressure and TTL support.
 *
 * This class provides isolated message queuing per subscriber with configurable
 * backpressure policies and TTL enforcement. It ensures slow consumers don't
 * affect fast consumers (no "noisy neighbor" problem).
 *
 * Copyright (c) 2024 NexusD Contributors
 * License: MIT
 */

#pragma once

#include "nexusd/core/export.hpp"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <atomic>

namespace nexusd {
namespace core {

/**
 * @brief Backpressure policy when queue is full.
 */
enum class BackpressurePolicy {
    DROP_OLDEST,  ///< Remove oldest message to make room (default)
    DROP_NEWEST,  ///< Reject new message when full
    BLOCK         ///< Block publisher until space available (with timeout)
};

/**
 * @brief Result of an enqueue operation.
 */
enum class EnqueueResult {
    ENQUEUED,             ///< Message successfully enqueued
    DROPPED_OLDEST,       ///< Oldest message dropped to make room
    DROPPED_NEWEST,       ///< This message rejected (queue full)
    DROPPED_TTL,          ///< Message expired before delivery
    DROPPED_DISCONNECTED, ///< Subscriber disconnected
    BLOCKED               ///< Publisher was blocked (for metrics)
};

/**
 * @brief Statistics for a subscriber queue.
 */
struct QueueStats {
    size_t current_depth = 0;
    size_t limit = 0;
    uint64_t total_enqueued = 0;
    uint64_t total_delivered = 0;
    uint64_t dropped_oldest = 0;
    uint64_t dropped_newest = 0;
    uint64_t dropped_ttl = 0;
    uint64_t block_timeouts = 0;
    size_t high_watermark = 0;
};

/**
 * @brief Internal message wrapper with TTL support.
 */
template<typename T>
struct QueuedMessage {
    T message;
    std::chrono::steady_clock::time_point expiry_time;
    std::chrono::steady_clock::time_point enqueue_time;
};

/**
 * @brief Per-subscriber message queue with backpressure and TTL.
 *
 * Thread-safe. Designed as a template for use with any message type.
 *
 * @tparam T Message type (e.g., sidecar::MessageEvent)
 */
template<typename T>
class SubscriberQueue {
public:
    /**
     * @brief Construct a subscriber queue.
     * @param limit Maximum messages in queue (0 = unlimited).
     * @param policy Backpressure policy when queue is full.
     * @param block_timeout_ms Timeout for BLOCK policy (ms).
     * @param default_ttl_ms Default TTL for messages (0 = infinite).
     */
    SubscriberQueue(
        uint32_t limit = 10000,
        BackpressurePolicy policy = BackpressurePolicy::DROP_OLDEST,
        int64_t block_timeout_ms = 5000,
        int64_t default_ttl_ms = 0
    )
        : limit_(limit)
        , policy_(policy)
        , block_timeout_ms_(block_timeout_ms)
        , default_ttl_ms_(default_ttl_ms)
    {}

    ~SubscriberQueue() {
        close();
    }

    // Non-copyable
    SubscriberQueue(const SubscriberQueue&) = delete;
    SubscriberQueue& operator=(const SubscriberQueue&) = delete;

    /**
     * @brief Enqueue a message for delivery.
     * @param msg The message to enqueue.
     * @param message_ttl_ms TTL from publisher (0 = use default).
     * @return EnqueueResult indicating what happened.
     */
    EnqueueResult enqueue(T msg, int64_t message_ttl_ms = 0) {
        if (closed_.load()) {
            return EnqueueResult::DROPPED_DISCONNECTED;
        }

        auto now = std::chrono::steady_clock::now();
        QueuedMessage<T> qm;
        qm.message = std::move(msg);
        qm.enqueue_time = now;

        // Calculate expiry time
        int64_t effective_ttl = message_ttl_ms > 0 ? message_ttl_ms : default_ttl_ms_;
        if (effective_ttl > 0) {
            qm.expiry_time = now + std::chrono::milliseconds(effective_ttl);
        } else {
            qm.expiry_time = std::chrono::steady_clock::time_point::max();
        }

        std::unique_lock<std::mutex> lock(mutex_);

        // Check if queue is full
        if (limit_ > 0 && messages_.size() >= limit_) {
            switch (policy_) {
                case BackpressurePolicy::DROP_OLDEST:
                    if (!messages_.empty()) {
                        messages_.pop_front();
                        stats_.dropped_oldest++;
                    }
                    break;

                case BackpressurePolicy::DROP_NEWEST:
                    stats_.dropped_newest++;
                    return EnqueueResult::DROPPED_NEWEST;

                case BackpressurePolicy::BLOCK: {
                    auto timeout = std::chrono::milliseconds(block_timeout_ms_);
                    bool got_space = cv_.wait_for(lock, timeout, [this]() {
                        return closed_.load() || messages_.size() < limit_;
                    });

                    if (!got_space || closed_.load()) {
                        stats_.block_timeouts++;
                        // Fall back to drop oldest
                        if (!messages_.empty()) {
                            messages_.pop_front();
                            stats_.dropped_oldest++;
                        }
                    }
                    break;
                }
            }
        }

        messages_.push_back(std::move(qm));
        stats_.total_enqueued++;

        if (messages_.size() > stats_.high_watermark) {
            stats_.high_watermark = messages_.size();
        }

        lock.unlock();
        cv_.notify_one();

        return EnqueueResult::ENQUEUED;
    }

    /**
     * @brief Dequeue the next message.
     * @return The next valid message, or nullopt if queue is empty.
     */
    std::optional<T> dequeue() {
        std::unique_lock<std::mutex> lock(mutex_);

        auto now = std::chrono::steady_clock::now();

        // Skip expired messages
        while (!messages_.empty()) {
            if (messages_.front().expiry_time <= now) {
                messages_.pop_front();
                stats_.dropped_ttl++;
            } else {
                break;
            }
        }

        if (messages_.empty()) {
            return std::nullopt;
        }

        auto msg = std::move(messages_.front().message);
        messages_.pop_front();
        stats_.total_delivered++;

        // Notify blocked producers
        if (policy_ == BackpressurePolicy::BLOCK) {
            lock.unlock();
            cv_.notify_one();
        }

        return msg;
    }

    /**
     * @brief Remove expired messages from queue.
     * @return Number of messages expired.
     */
    size_t reapExpired() {
        std::unique_lock<std::mutex> lock(mutex_);

        auto now = std::chrono::steady_clock::now();
        size_t expired_count = 0;

        while (!messages_.empty() && messages_.front().expiry_time <= now) {
            messages_.pop_front();
            expired_count++;
            stats_.dropped_ttl++;
        }

        if (expired_count > 0 && policy_ == BackpressurePolicy::BLOCK) {
            lock.unlock();
            cv_.notify_all();
        }

        return expired_count;
    }

    /**
     * @brief Get current queue statistics.
     */
    QueueStats getStats() const {
        std::unique_lock<std::mutex> lock(mutex_);
        QueueStats result = stats_;
        result.current_depth = messages_.size();
        result.limit = limit_;
        return result;
    }

    /**
     * @brief Get current queue depth.
     */
    size_t size() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return messages_.size();
    }

    /**
     * @brief Check if queue is empty.
     */
    bool empty() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return messages_.empty();
    }

    /**
     * @brief Close the queue.
     */
    void close() {
        closed_.store(true);
        cv_.notify_all();
    }

    bool isClosed() const {
        return closed_.load();
    }

private:
    uint32_t limit_;
    BackpressurePolicy policy_;
    int64_t block_timeout_ms_;
    int64_t default_ttl_ms_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<QueuedMessage<T>> messages_;
    std::atomic<bool> closed_{false};
    QueueStats stats_;
};

}  // namespace core
}  // namespace nexusd
