/**
 * @file discovery_agent.cpp
 * @brief DiscoveryAgent implementation.
 *
 * @copyright Copyright (c) 2024 NexusD Contributors
 * @license MIT License
 */

#include "nexusd/core/discovery_agent.hpp"
#include "nexusd/utils/logger.hpp"

// Include generated protobuf header
#include "nexusd/proto/discovery.pb.h"

#include <chrono>

namespace nexusd {
namespace core {

DiscoveryAgent::DiscoveryAgent(const DiscoveryConfig& config,
                               std::shared_ptr<PeerRegistry> registry)
    : config_(config)
    , registry_(std::move(registry))
    , currentRpcIp_(config.rpc_ip)
    , currentRpcPort_(config.rpc_port)
{
    LOG_INFO("Discovery", "Created agent for cluster '{}' on {}:{}",
             config_.cluster_id, config_.mcast_addr, config_.mcast_port);
}

DiscoveryAgent::~DiscoveryAgent() {
    stop();
}

void DiscoveryAgent::setSyncCallback(SyncPeerCallback callback) {
    syncCallback_ = std::move(callback);
}

bool DiscoveryAgent::start() {
    if (running_.load()) {
        LOG_WARN("Discovery", "Agent already running");
        return false;
    }

    // Configure socket
    if (!socket_.isValid()) {
        LOG_ERROR("Discovery", "Socket not valid");
        return false;
    }

    if (!socket_.setReuseAddress(true)) {
        LOG_ERROR("Discovery", "Failed to set SO_REUSEADDR");
        return false;
    }

    if (!socket_.bind(config_.mcast_port)) {
        LOG_ERROR("Discovery", "Failed to bind to port {}", config_.mcast_port);
        return false;
    }

    if (!socket_.joinMulticastGroup(config_.mcast_addr)) {
        LOG_ERROR("Discovery", "Failed to join multicast group {}", config_.mcast_addr);
        return false;
    }

    if (!socket_.setMulticastTTL(config_.multicast_ttl)) {
        LOG_WARN("Discovery", "Failed to set multicast TTL");
    }

    if (!socket_.setMulticastLoopback(config_.loopback)) {
        LOG_WARN("Discovery", "Failed to set multicast loopback");
    }

    running_.store(true);

    // Start threads
    senderThread_ = std::thread(&DiscoveryAgent::senderLoop, this);
    receiverThread_ = std::thread(&DiscoveryAgent::receiverLoop, this);

    LOG_INFO("Discovery", "Agent started on {}:{}", 
             config_.mcast_addr, config_.mcast_port);
    return true;
}

void DiscoveryAgent::stop() {
    if (!running_.load()) {
        return;
    }

    LOG_INFO("Discovery", "Stopping agent...");
    running_.store(false);

    // Close socket to unblock receiver
    socket_.close();

    // Wait for threads
    if (senderThread_.joinable()) {
        senderThread_.join();
    }
    if (receiverThread_.joinable()) {
        receiverThread_.join();
    }

    LOG_INFO("Discovery", "Agent stopped");
}

uint16_t DiscoveryAgent::getLocalPort() const {
    return socket_.getLocalPort();
}

void DiscoveryAgent::updateRpcEndpoint(const std::string& ip, uint16_t port) {
    std::lock_guard<std::mutex> lock(configMutex_);
    currentRpcIp_ = ip;
    currentRpcPort_ = port;
    LOG_INFO("Discovery", "Updated RPC endpoint to {}:{}", ip, port);
}

void DiscoveryAgent::senderLoop() {
    LOG_DEBUG("Discovery", "Sender thread started");

    while (running_.load()) {
        sendBeacon();

        // Sleep for beacon interval
        std::this_thread::sleep_for(
            std::chrono::milliseconds(config_.beacon_interval_ms));
    }

    LOG_DEBUG("Discovery", "Sender thread stopped");
}

void DiscoveryAgent::receiverLoop() {
    LOG_DEBUG("Discovery", "Receiver thread started");

    std::vector<uint8_t> buffer(65535);
    auto lastReap = std::chrono::steady_clock::now();

    while (running_.load()) {
        net::SocketAddress sender;
        
        // Receive with 500ms timeout to allow checking running flag
        int received = socket_.receiveFrom(buffer.data(), buffer.size(), 500, sender);

        if (received > 0) {
            processBeacon(buffer.data(), static_cast<size_t>(received), sender);
        } else if (received < 0 && running_.load()) {
            // Error (but not timeout)
            LOG_ERROR("Discovery", "Receive error: {}", socket_.getLastError());
        }

        // Periodically reap dead peers
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastReap).count() >= 1000) {
            reapDeadPeers();
            lastReap = now;
        }
    }

    LOG_DEBUG("Discovery", "Receiver thread stopped");
}

void DiscoveryAgent::sendBeacon() {
    // Build beacon
    nexusd::discovery::Beacon beacon;
    beacon.set_instance_uuid(registry_->getLocalNodeId());
    beacon.set_cluster_id(config_.cluster_id);
    
    {
        std::lock_guard<std::mutex> lock(configMutex_);
        beacon.set_rpc_ip(currentRpcIp_);
        beacon.set_rpc_port(static_cast<int32_t>(currentRpcPort_));
    }
    
    beacon.set_topic_state_hash(registry_->getLocalTopicHash());
    
    auto now = std::chrono::system_clock::now();
    beacon.set_timestamp_ms(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count());
    beacon.set_protocol_version(1);

    // Serialize
    std::string data;
    if (!beacon.SerializeToString(&data)) {
        LOG_ERROR("Discovery", "Failed to serialize beacon");
        return;
    }

    // Send
    net::SocketAddress dest(config_.mcast_addr, config_.mcast_port);
    int sent = socket_.sendTo(dest, data.data(), data.size());
    
    if (sent < 0) {
        LOG_ERROR("Discovery", "Failed to send beacon: {}", socket_.getLastError());
    } else {
        LOG_TRACE("Discovery", "Sent beacon ({} bytes)", sent);
    }
}

void DiscoveryAgent::processBeacon(const uint8_t* data, size_t length,
                                   const net::SocketAddress& sender) {
    // Parse beacon
    nexusd::discovery::Beacon beacon;
    if (!beacon.ParseFromArray(data, static_cast<int>(length))) {
        LOG_WARN("Discovery", "Failed to parse beacon from {}", sender.toString());
        return;
    }

    // Ignore our own beacons
    if (beacon.instance_uuid() == registry_->getLocalNodeId()) {
        return;
    }

    // Filter by cluster ID (the "bouncer")
    if (beacon.cluster_id() != config_.cluster_id) {
        LOG_TRACE("Discovery", "Ignoring beacon from {} (cluster '{}')",
                  beacon.instance_uuid(), beacon.cluster_id());
        return;
    }

    LOG_TRACE("Discovery", "Received beacon from {} at {}:{}",
              beacon.instance_uuid(), beacon.rpc_ip(), beacon.rpc_port());

    // Build PeerInfo
    PeerInfo info;
    info.instance_uuid = beacon.instance_uuid();
    info.cluster_id = beacon.cluster_id();
    info.rpc_ip = beacon.rpc_ip();
    info.rpc_port = static_cast<uint16_t>(beacon.rpc_port());
    info.topic_state_hash = beacon.topic_state_hash();
    info.status = PeerStatus::ALIVE;
    info.last_seen = std::chrono::steady_clock::now();

    // Update registry
    bool needsSync = registry_->upsertPeer(info);

    // Trigger sync if hash changed
    if (needsSync && syncCallback_) {
        auto peer = registry_->getPeer(info.instance_uuid);
        if (peer) {
            LOG_DEBUG("Discovery", "Triggering sync with peer {} (hash mismatch)",
                      info.instance_uuid);
            syncCallback_(*peer);
        }
    }
}

void DiscoveryAgent::reapDeadPeers() {
    auto deadPeers = registry_->reapDeadPeers(
        std::chrono::milliseconds(config_.peer_timeout_ms));
    
    for (const auto& peerId : deadPeers) {
        LOG_WARN("Discovery", "Reaped dead peer: {}", peerId);
    }
}

}  // namespace core
}  // namespace nexusd
