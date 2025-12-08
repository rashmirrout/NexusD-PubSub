# NexusD Design Document

## Overview

NexusD is a brokerless, distributed publish/subscribe middleware implementing the sidecar pattern. This document details the internal design decisions, protocols, and module responsibilities.

---

## Architecture Layers

```
┌─────────────────────────────────────────────────────────────────┐
│                        Application Layer                         │
│                    (gRPC SidecarService API)                     │
├─────────────────────────────────────────────────────────────────┤
│                        Services Layer                            │
│              MeshService │ SidecarService │ MeshClient           │
├─────────────────────────────────────────────────────────────────┤
│                          Core Layer                              │
│                 PeerRegistry │ DiscoveryAgent                    │
├─────────────────────────────────────────────────────────────────┤
│                        Network Layer                             │
│                   UDPSocket (Cross-platform)                     │
├─────────────────────────────────────────────────────────────────┤
│                        Utility Layer                             │
│                   Logger │ UUID │ CRC64                          │
└─────────────────────────────────────────────────────────────────┘
```

---

## Gossip Protocol Implementation

The gossip protocol enables lazy synchronization of subscription state across the mesh. It is implemented across two primary modules:

### Module 1: `nexusd_core` - Discovery Agent

**Files:** `include/nexusd/core/discovery_agent.hpp`, `src/core/discovery_agent.cpp`

The Discovery Agent handles **hash-triggered gossip detection**:

| Responsibility | Description |
|----------------|-------------|
| Beacon Sender | Broadcasts UDP multicast beacons at 1Hz containing node state |
| Beacon Receiver | Listens for peer beacons and updates PeerRegistry |
| Hash Comparison | Compares local `topic_state_hash` with peer's hash |
| Sync Trigger | Initiates gossip sync when hash mismatch detected |
| Peer Reaper | Removes stale peers after 5-second timeout |

**Beacon Payload:**
```protobuf
message Beacon {
    string instance_uuid = 1;      // Unique node identifier
    string cluster_id = 2;         // Cluster isolation boundary
    string rpc_ip = 3;             // Mesh gRPC endpoint IP
    uint32 rpc_port = 4;           // Mesh gRPC endpoint port
    uint64 topic_state_hash = 5;   // CRC64 of subscribed topics
}
```

**Hash Calculation:**
```cpp
// CRC64-ECMA-182 of sorted topic names
uint64_t computeTopicStateHash(const std::string& node_id) {
    auto topics = getNodeSubscriptions(node_id);
    std::sort(topics.begin(), topics.end());
    std::string concat;
    for (const auto& t : topics) concat += t + "\n";
    return crc64String(concat);
}
```

### Module 2: `nexusd_services` - Mesh Client & Service

**Files:** 
- `include/nexusd/services/mesh_client.hpp`, `src/services/mesh_client.cpp`
- `include/nexusd/services/mesh_service.hpp`, `src/services/mesh_service.cpp`

These handle the **actual gossip sync exchange**:

| Component | Responsibility |
|-----------|----------------|
| **MeshClient** | Initiates `GetNodeState` RPC to fetch peer's subscriptions |
| **MeshService** | Responds to `GetNodeState` with local subscription list |
| **MeshService** | Handles `PushMessage` RPC for message forwarding |

**Gossip Sync Protocol:**
```protobuf
service MeshService {
    // Fetch peer's subscription state for gossip sync
    rpc GetNodeState(GetNodeStateRequest) returns (GetNodeStateResponse);
    
    // Forward published messages to subscribers on this node
    rpc PushMessage(PushMessageRequest) returns (PushMessageResponse);
}

message GetNodeStateResponse {
    repeated string subscribed_topics = 1;  // All topics this node subscribes to
    uint64 topic_state_hash = 2;            // Current hash for verification
}
```

### Gossip Flow Diagram

```
Node A                                    Node B
  │                                          │
  │  ┌─────────────────────────────────┐     │
  │  │ Beacon Sender Thread (1Hz)      │     │
  │  └─────────────────────────────────┘     │
  │                                          │
  ├──[UDP Multicast Beacon]─────────────────►│
  │   instance_uuid: "node-a-uuid"           │
  │   cluster_id: "production"               │
  │   rpc_ip: "192.168.1.10"                 │
  │   rpc_port: 5671                         │
  │   topic_state_hash: 0xABCD1234           │
  │                                          │
  │◄─────────────[UDP Multicast Beacon]──────┤
  │   instance_uuid: "node-b-uuid"           │
  │   topic_state_hash: 0xDEF56789           │
  │                                          │
  │  ┌─────────────────────────────────┐     │
  │  │ Hash Mismatch Detected!         │     │
  │  │ 0xABCD1234 != 0xDEF56789        │     │
  │  └─────────────────────────────────┘     │
  │                                          │
  │  ┌─────────────────────────────────┐     │
  │  │ MeshClient triggers sync        │     │
  │  └─────────────────────────────────┘     │
  │                                          │
  ├──[gRPC GetNodeState Request]────────────►│
  │                                          │
  │◄─────────[GetNodeStateResponse]──────────┤
  │   subscribed_topics: ["sensor/temp",     │
  │                       "sensor/humidity"] │
  │   topic_state_hash: 0xDEF56789           │
  │                                          │
  │  ┌─────────────────────────────────┐     │
  │  │ Update Local Routing Table      │     │
  │  │ node-b → [sensor/temp,          │     │
  │  │           sensor/humidity]      │     │
  │  └─────────────────────────────────┘     │
  │                                          │
```

### Why Hash-Triggered Gossip?

| Approach | Network Traffic | Complexity |
|----------|-----------------|------------|
| Full sync every heartbeat | High | Low |
| Push on every subscription change | Medium | Medium |
| **Hash-triggered (NexusD)** | **Low** | **Medium** |

Benefits:
1. **Minimal overhead**: Sync only when state actually changes
2. **Distributed consistency**: CRC64 ensures deterministic comparison
3. **Scalability**: O(1) comparison per beacon, O(n) sync only when needed

---

## Module Responsibilities

### `nexusd_utils` (No Dependencies)

| Component | Purpose |
|-----------|---------|
| `Logger` | Thread-safe logging with levels TRACE→FATAL, component tags |
| `UUID` | RFC 4122 UUID v4 generation using `<random>` |
| `CRC64` | ECMA-182 polynomial for topic state hashing |

### `nexusd_net` (Depends on: utils)

| Component | Purpose |
|-----------|---------|
| `UDPSocket` | Cross-platform UDP with multicast support |
| `platform.hpp` | Winsock2 / POSIX abstraction |

### `nexusd_proto` (Depends on: gRPC)

| Component | Purpose |
|-----------|---------|
| Generated code | Protobuf messages and gRPC stubs |

### `nexusd_core` (Depends on: utils, net, proto)

| Component | Purpose |
|-----------|---------|
| `PeerRegistry` | Thread-safe peer tracking, routing table, retained messages |
| `DiscoveryAgent` | UDP beacon sender/receiver, peer reaper, gossip trigger |

### `nexusd_services` (Depends on: core, proto, gRPC)

| Component | Purpose |
|-----------|---------|
| `MeshClient` | Async gRPC client for GetNodeState, PushMessage |
| `MeshService` | gRPC server handling mesh RPCs |
| `SidecarService` | gRPC server for application Publish/Subscribe API |

---

## Threading Model

```
┌─────────────────────────────────────────────────────────────┐
│                      Main Thread                             │
│  - Initialization                                            │
│  - Signal handling                                           │
│  - Graceful shutdown                                         │
└─────────────────────────────────────────────────────────────┘
         │
         ├─────────────────────────────────────────────────────┐
         │                                                     │
         ▼                                                     ▼
┌─────────────────────┐                         ┌─────────────────────┐
│ Discovery Sender    │                         │ Discovery Receiver  │
│ Thread              │                         │ Thread              │
│ - 1Hz beacon loop   │                         │ - UDP recvfrom()    │
│ - Compute hash      │                         │ - Parse beacon      │
│ - UDP sendto()      │                         │ - Update registry   │
└─────────────────────┘                         │ - Trigger sync      │
                                                └─────────────────────┘
         │
         ├─────────────────────────────────────────────────────┐
         │                                                     │
         ▼                                                     ▼
┌─────────────────────┐                         ┌─────────────────────┐
│ Mesh gRPC Server    │                         │ App gRPC Server     │
│ (port 5671)         │                         │ (port 5672)         │
│ - GetNodeState      │                         │ - Publish           │
│ - PushMessage       │                         │ - Subscribe (stream)│
│ - Async callbacks   │                         │ - Unsubscribe       │
└─────────────────────┘                         └─────────────────────┘
```

---

## Data Structures

### PeerRegistry

```cpp
class PeerRegistry {
    // Peer information (thread-safe with shared_mutex)
    Map<NodeID, PeerInfo> peers_;
    
    // Routing table: topic → set of subscribed nodes
    Map<Topic, Set<NodeID>> topic_subscribers_;
    
    // Reverse index: node → set of subscribed topics
    Map<NodeID, Set<Topic>> node_subscriptions_;
    
    // Retained messages per topic
    Map<Topic, vector<uint8_t>> retained_messages_;
};
```

### PeerInfo

```cpp
struct PeerInfo {
    std::string instance_uuid;
    std::string cluster_id;
    std::string rpc_ip;
    uint16_t rpc_port;
    uint64_t topic_state_hash;
    std::chrono::steady_clock::time_point last_seen;
};
```

---

## Configuration

| Parameter | Default | Environment | CLI Flag |
|-----------|---------|-------------|----------|
| Cluster ID | `default` | `NEXUSD_CLUSTER` | `--cluster` |
| Multicast Address | `239.255.42.1` | `NEXUSD_MCAST_ADDR` | `--mcast-addr` |
| Multicast Port | `5670` | `NEXUSD_MCAST_PORT` | `--mcast-port` |
| Mesh Port | `5671` | `NEXUSD_MESH_PORT` | `--mesh-port` |
| App Port | `5672` | `NEXUSD_APP_PORT` | `--app-port` |
| Log Level | `INFO` | `NEXUSD_LOG_LEVEL` | `--log-level` |

---

## Error Handling

| Scenario | Behavior |
|----------|----------|
| Peer timeout (5s) | Peer removed from registry, subscriptions cleaned |
| gRPC call failure | Logged, peer marked unhealthy, retry on next beacon |
| UDP socket error | Logged, discovery continues with available sockets |
| Invalid beacon | Silently dropped (wrong cluster or malformed) |

---

## Future Considerations

1. **TLS for Mesh gRPC**: Currently uses InsecureChannelCredentials
2. **Wildcard Subscriptions**: Support for `sensor/#` patterns
3. **QoS Levels**: At-most-once, at-least-once, exactly-once
4. **Persistence**: Disk-backed retained messages
5. **Metrics**: Prometheus endpoint for monitoring
