/**
 * @file peer_registry.cpp
 * @brief PeerRegistry implementation.
 *
 * @copyright Copyright (c) 2024 NexusD Contributors
 * @license MIT License
 */

#include "nexusd/core/peer_registry.hpp"
#include "nexusd/utils/logger.hpp"

#include <algorithm>

namespace nexusd {
namespace core {

PeerRegistry::PeerRegistry(const std::string& localNodeId)
    : localNodeId_(localNodeId)
{
    LOG_INFO("PeerRegistry", "Created registry for node {}", localNodeId_);
}

// =============================================================================
// Peer Management
// =============================================================================

bool PeerRegistry::upsertPeer(const PeerInfo& info) {
    std::unique_lock<std::shared_mutex> lock(peerMutex_);

    auto it = peers_.find(info.instance_uuid);
    bool needsSync = false;

    if (it == peers_.end()) {
        // New peer
        auto peer = std::make_shared<PeerInfo>(info);
        peers_[info.instance_uuid] = peer;
        needsSync = true;
        LOG_INFO("PeerRegistry", "New peer discovered: {} at {}",
                 info.instance_uuid, info.endpoint());
    } else {
        // Existing peer - update
        auto& peer = it->second;
        
        // Check if hash changed
        if (peer->topic_state_hash != info.topic_state_hash) {
            needsSync = true;
            peer->synced = false;
            LOG_DEBUG("PeerRegistry", "Peer {} hash changed: {} -> {}",
                      info.instance_uuid, peer->topic_state_hash, info.topic_state_hash);
        }

        peer->rpc_ip = info.rpc_ip;
        peer->rpc_port = info.rpc_port;
        peer->topic_state_hash = info.topic_state_hash;
        peer->status = PeerStatus::ALIVE;
        peer->last_seen = std::chrono::steady_clock::now();
    }

    return needsSync;
}

void PeerRegistry::removePeer(const std::string& instanceUuid) {
    {
        std::unique_lock<std::shared_mutex> lock(peerMutex_);
        auto it = peers_.find(instanceUuid);
        if (it != peers_.end()) {
            LOG_INFO("PeerRegistry", "Removing peer: {}", instanceUuid);
            peers_.erase(it);
        }
    }

    // Rebuild routing table
    rebuildRoutingTable();
}

std::shared_ptr<PeerInfo> PeerRegistry::getPeer(const std::string& instanceUuid) const {
    std::shared_lock<std::shared_mutex> lock(peerMutex_);
    auto it = peers_.find(instanceUuid);
    if (it != peers_.end()) {
        return std::make_shared<PeerInfo>(*it->second);
    }
    return nullptr;
}

std::vector<PeerInfo> PeerRegistry::getAllPeers() const {
    std::shared_lock<std::shared_mutex> lock(peerMutex_);
    std::vector<PeerInfo> result;
    result.reserve(peers_.size());
    for (const auto& [id, peer] : peers_) {
        result.push_back(*peer);
    }
    return result;
}

void PeerRegistry::updatePeerState(const std::string& instanceUuid,
                                   const std::vector<std::string>& topics,
                                   uint64_t hash) {
    {
        std::unique_lock<std::shared_mutex> lock(peerMutex_);
        auto it = peers_.find(instanceUuid);
        if (it != peers_.end()) {
            it->second->topics = topics;
            it->second->topic_state_hash = hash;
            it->second->synced = true;
            LOG_DEBUG("PeerRegistry", "Updated peer {} state: {} topics, hash={}",
                      instanceUuid, topics.size(), hash);
        }
    }

    // Rebuild routing table with new peer topics
    rebuildRoutingTable();
}

std::vector<std::string> PeerRegistry::reapDeadPeers(std::chrono::milliseconds timeout) {
    std::vector<std::string> deadPeers;
    auto now = std::chrono::steady_clock::now();

    {
        std::unique_lock<std::shared_mutex> lock(peerMutex_);
        
        for (auto it = peers_.begin(); it != peers_.end(); ) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - it->second->last_seen);
            
            if (elapsed > timeout) {
                LOG_WARN("PeerRegistry", "Peer {} timed out after {}ms",
                         it->first, elapsed.count());
                deadPeers.push_back(it->first);
                it = peers_.erase(it);
            } else {
                ++it;
            }
        }
    }

    if (!deadPeers.empty()) {
        rebuildRoutingTable();
    }

    return deadPeers;
}

// =============================================================================
// Local Subscription Management
// =============================================================================

bool PeerRegistry::addLocalSubscription(
    const std::string& subscriptionId,
    const std::string& clientId,
    const std::vector<std::string>& topics,
    int32_t maxBufferSize,
    std::function<void(const RetainedMessage&)> callback) {
    
    std::unique_lock<std::shared_mutex> lock(subscriptionMutex_);

    if (localSubscriptions_.count(subscriptionId)) {
        LOG_WARN("PeerRegistry", "Subscription {} already exists", subscriptionId);
        return false;
    }

    auto sub = std::make_shared<LocalSubscription>();
    sub->subscription_id = subscriptionId;
    sub->client_id = clientId;
    sub->topics = topics;
    sub->max_buffer_size = maxBufferSize;
    sub->created_at = std::chrono::steady_clock::now();
    sub->deliverCallback = std::move(callback);

    localSubscriptions_[subscriptionId] = sub;

    // Update topic -> subscription mapping
    for (const auto& topic : topics) {
        topicToSubscriptions_[topic].insert(subscriptionId);
    }

    // Mark hash as dirty
    hashDirty_.store(true);

    LOG_INFO("PeerRegistry", "Added subscription {} for {} topics",
             subscriptionId, topics.size());

    return true;
}

bool PeerRegistry::removeLocalSubscription(const std::string& subscriptionId,
                                           bool pause,
                                           int64_t ttl_ms) {
    std::unique_lock<std::shared_mutex> lock(subscriptionMutex_);

    auto it = localSubscriptions_.find(subscriptionId);
    if (it == localSubscriptions_.end()) {
        return false;
    }

    // If pausing, save the subscription state
    if (pause) {
        auto paused = std::make_shared<PausedSubscription>();
        paused->subscription_id = subscriptionId;
        paused->client_id = it->second->client_id;
        paused->topics = it->second->topics;
        paused->max_buffer_size = it->second->max_buffer_size;
        paused->last_delivered_sequence = it->second->last_delivered_sequence;
        paused->paused_at = std::chrono::steady_clock::now();
        paused->expires_at = paused->paused_at + std::chrono::milliseconds(ttl_ms);
        
        pausedSubscriptions_[subscriptionId] = paused;
        LOG_INFO("PeerRegistry", "Paused subscription {} with TTL {}ms", 
                 subscriptionId, ttl_ms);
    }

    // Remove from topic mapping
    for (const auto& topic : it->second->topics) {
        auto topicIt = topicToSubscriptions_.find(topic);
        if (topicIt != topicToSubscriptions_.end()) {
            topicIt->second.erase(subscriptionId);
            if (topicIt->second.empty()) {
                topicToSubscriptions_.erase(topicIt);
            }
        }
    }

    localSubscriptions_.erase(it);
    hashDirty_.store(true);

    LOG_INFO("PeerRegistry", "Removed subscription {}", subscriptionId);
    return true;
}

bool PeerRegistry::pauseSubscription(const std::string& subscriptionId, int64_t ttl_ms) {
    return removeLocalSubscription(subscriptionId, true, ttl_ms);
}

bool PeerRegistry::resumeSubscription(
    const std::string& subscriptionId,
    std::function<void(const RetainedMessage&)> callback,
    PausedSubscription& out_paused_info) {
    
    std::unique_lock<std::shared_mutex> lock(subscriptionMutex_);
    
    auto pausedIt = pausedSubscriptions_.find(subscriptionId);
    if (pausedIt == pausedSubscriptions_.end()) {
        LOG_WARN("PeerRegistry", "No paused subscription found: {}", subscriptionId);
        return false;
    }
    
    auto now = std::chrono::steady_clock::now();
    if (now > pausedIt->second->expires_at) {
        LOG_WARN("PeerRegistry", "Paused subscription {} has expired", subscriptionId);
        pausedSubscriptions_.erase(pausedIt);
        return false;
    }
    
    // Copy paused info for gap detection
    out_paused_info = *pausedIt->second;
    
    // Recreate the active subscription
    auto sub = std::make_shared<LocalSubscription>();
    sub->subscription_id = subscriptionId;
    sub->client_id = pausedIt->second->client_id;
    sub->topics = pausedIt->second->topics;
    sub->max_buffer_size = pausedIt->second->max_buffer_size;
    sub->created_at = std::chrono::steady_clock::now();
    sub->last_delivered_sequence = pausedIt->second->last_delivered_sequence;
    sub->deliverCallback = std::move(callback);
    
    localSubscriptions_[subscriptionId] = sub;
    
    // Restore topic -> subscription mapping
    for (const auto& topic : sub->topics) {
        topicToSubscriptions_[topic].insert(subscriptionId);
    }
    
    // Remove from paused
    pausedSubscriptions_.erase(pausedIt);
    
    hashDirty_.store(true);
    
    LOG_INFO("PeerRegistry", "Resumed subscription {} after pause", subscriptionId);
    return true;
}

std::shared_ptr<PausedSubscription> PeerRegistry::getPausedSubscription(
    const std::string& subscriptionId) const {
    
    std::shared_lock<std::shared_mutex> lock(subscriptionMutex_);
    
    auto it = pausedSubscriptions_.find(subscriptionId);
    if (it != pausedSubscriptions_.end()) {
        return std::make_shared<PausedSubscription>(*it->second);
    }
    return nullptr;
}

size_t PeerRegistry::clearExpiredPausedSubscriptions() {
    std::unique_lock<std::shared_mutex> lock(subscriptionMutex_);
    
    auto now = std::chrono::steady_clock::now();
    size_t count = 0;
    
    for (auto it = pausedSubscriptions_.begin(); it != pausedSubscriptions_.end(); ) {
        if (now > it->second->expires_at) {
            LOG_DEBUG("PeerRegistry", "Expired paused subscription: {}", it->first);
            it = pausedSubscriptions_.erase(it);
            ++count;
        } else {
            ++it;
        }
    }
    
    if (count > 0) {
        LOG_INFO("PeerRegistry", "Cleared {} expired paused subscriptions", count);
    }
    
    return count;
}

void PeerRegistry::updateSubscriptionSequence(const std::string& subscriptionId,
                                              const std::string& topic,
                                              uint64_t sequence) {
    std::unique_lock<std::shared_mutex> lock(subscriptionMutex_);
    
    auto it = localSubscriptions_.find(subscriptionId);
    if (it != localSubscriptions_.end()) {
        it->second->last_delivered_sequence = sequence;
    }
}

std::vector<std::shared_ptr<LocalSubscription>>
PeerRegistry::getLocalSubscriptions(const std::string& topic) const {
    std::shared_lock<std::shared_mutex> lock(subscriptionMutex_);
    
    std::vector<std::shared_ptr<LocalSubscription>> result;
    
    auto it = topicToSubscriptions_.find(topic);
    if (it != topicToSubscriptions_.end()) {
        for (const auto& subId : it->second) {
            auto subIt = localSubscriptions_.find(subId);
            if (subIt != localSubscriptions_.end()) {
                result.push_back(subIt->second);
            }
        }
    }
    
    return result;
}

std::vector<std::string> PeerRegistry::getLocalTopics() const {
    std::shared_lock<std::shared_mutex> lock(subscriptionMutex_);
    
    std::vector<std::string> topics;
    topics.reserve(topicToSubscriptions_.size());
    
    for (const auto& [topic, subs] : topicToSubscriptions_) {
        topics.push_back(topic);
    }
    
    return topics;
}

// =============================================================================
// Routing Table
// =============================================================================

std::vector<std::string> PeerRegistry::getRemoteSubscribers(const std::string& topic) const {
    std::shared_lock<std::shared_mutex> lock(routingMutex_);
    
    std::vector<std::string> result;
    auto it = routingTable_.find(topic);
    if (it != routingTable_.end()) {
        result.reserve(it->second.size());
        for (const auto& peerId : it->second) {
            result.push_back(peerId);
        }
    }
    
    return result;
}

bool PeerRegistry::hasSubscribers(const std::string& topic) const {
    // Check remote
    {
        std::shared_lock<std::shared_mutex> lock(routingMutex_);
        if (routingTable_.count(topic)) {
            return true;
        }
    }
    
    // Check local
    {
        std::shared_lock<std::shared_mutex> lock(subscriptionMutex_);
        if (topicToSubscriptions_.count(topic)) {
            return true;
        }
    }
    
    return false;
}

std::unordered_map<std::string, std::unordered_set<std::string>>
PeerRegistry::getRoutingTable() const {
    std::shared_lock<std::shared_mutex> lock(routingMutex_);
    return routingTable_;
}

void PeerRegistry::rebuildRoutingTable() {
    std::unique_lock<std::shared_mutex> routingLock(routingMutex_);
    std::shared_lock<std::shared_mutex> peerLock(peerMutex_);

    routingTable_.clear();

    for (const auto& [peerId, peer] : peers_) {
        if (peer->synced) {
            for (const auto& topic : peer->topics) {
                routingTable_[topic].insert(peerId);
            }
        }
    }

    LOG_DEBUG("PeerRegistry", "Rebuilt routing table: {} topics",
              routingTable_.size());
}

// =============================================================================
// Topic Hash
// =============================================================================

uint64_t PeerRegistry::getLocalTopicHash() const {
    if (hashDirty_.load()) {
        cachedLocalHash_.store(computeLocalTopicHash());
        hashDirty_.store(false);
    }
    return cachedLocalHash_.load();
}

uint64_t PeerRegistry::computeLocalTopicHash() const {
    std::shared_lock<std::shared_mutex> lock(subscriptionMutex_);
    
    // Get sorted topic list
    std::vector<std::string> topics;
    topics.reserve(topicToSubscriptions_.size());
    for (const auto& [topic, _] : topicToSubscriptions_) {
        topics.push_back(topic);
    }
    std::sort(topics.begin(), topics.end());
    
    // Compute CRC64 over concatenated topics
    return utils::CRC64::compute(topics);
}

// =============================================================================
// Retained Messages
// =============================================================================

void PeerRegistry::setRetainedMessage(const std::string& topic,
                                      const RetainedMessage& message) {
    std::unique_lock<std::shared_mutex> lock(retainedMutex_);
    retainedMessages_[topic] = message;
    LOG_DEBUG("PeerRegistry", "Stored retained message for topic {}", topic);
}

std::shared_ptr<RetainedMessage> PeerRegistry::getRetainedMessage(
    const std::string& topic) const {
    std::shared_lock<std::shared_mutex> lock(retainedMutex_);
    auto it = retainedMessages_.find(topic);
    if (it != retainedMessages_.end()) {
        return std::make_shared<RetainedMessage>(it->second);
    }
    return nullptr;
}

std::unordered_map<std::string, RetainedMessage>
PeerRegistry::getAllRetainedMessages() const {
    std::shared_lock<std::shared_mutex> lock(retainedMutex_);
    return retainedMessages_;
}

void PeerRegistry::clearExpiredRetainedMessages() {
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    std::unique_lock<std::shared_mutex> lock(retainedMutex_);
    
    for (auto it = retainedMessages_.begin(); it != retainedMessages_.end(); ) {
        if (it->second.ttl_ms > 0) {
            int64_t age = now - it->second.timestamp_ms;
            if (age > it->second.ttl_ms) {
                LOG_DEBUG("PeerRegistry", "Expired retained message for topic {}",
                          it->first);
                it = retainedMessages_.erase(it);
                continue;
            }
        }
        ++it;
    }
}

}  // namespace core
}  // namespace nexusd
