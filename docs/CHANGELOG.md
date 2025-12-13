# NexusD Changelog

This changelog captures feature descriptions, design brainstorming, alternatives considered,
and final decisions. It serves as both a changelog and design decision record (ADR) to help
the community understand the reasoning behind implementation choices.

=============================================================================
## Feature: Backpressure Handling with Per-Subscriber Queues and TTL
Date: 2025-12-13 | Author: Community | Status: IMPLEMENTED
=============================================================================

### Problem Statement

When publishers send messages faster than subscribers can consume them, the daemon
experiences unbounded memory growth leading to OOM crashes. A single slow subscriber
can crash the entire daemon, affecting all other subscribers.

**Current behavior (problematic):**
```
Publisher (1000 msg/s) ──► Daemon Queue ──► Slow Subscriber (100 msg/s)
                              │
                              ▼
                    Queue grows 900 msg/s
                    After 1 hour: 3.24M messages queued
                    At 1KB/msg: 3.24 GB memory consumed
                              │
                              ▼
                         OOM CRASH 💥
```

### Design Alternatives Considered

#### Alternative 1: Global Queue Limit
- Single queue shared by all subscribers
- Simple implementation
- **Rejected**: Penalizes fast subscribers when slow subscriber fills the queue

#### Alternative 2: Per-Topic Queue Limit
- One queue per topic
- Moderate isolation
- **Rejected**: Multiple slow subscribers on same topic still affect each other

#### Alternative 3: Per-Subscriber Queue Limit ✅ SELECTED
- Each subscriber has isolated queue with independent limit
- Slow consumer only affects itself
- Fast consumers unaffected by slow peers
- **Selected**: Best isolation, fair resource allocation

### Discussion Points

**Q1: Global limit penalizes publishers whose consumers are fast. Is that fair?**
- A: No. Global limits cause "noisy neighbor" problems where one slow subscriber
  starves all others. Per-subscriber queues provide isolation.

**Q2: A single publisher can have multiple subscribers. When is a message invalidated?**
- A: Message is COPIED to each subscriber's queue at publish time. Each queue manages
  its own lifecycle independently:
  - Removed when successfully sent via gRPC
  - Removed when TTL expires
  - Removed when queue full (per drop policy)
  - Removed when subscriber disconnects

**Q3: Should blocking publishers have a timeout?**
- A: Yes. BLOCK policy waits for queue space with configurable timeout (default 5s).
  After timeout, falls back to drop policy to prevent indefinite publisher blocking.

**Q4: Should we implement TTL?**
- A: Yes. Two levels:
  1. Message-level TTL (publisher-specified via `ttl_ms` field - already in proto)
  2. Queue-level TTL (daemon-configured default for messages without explicit TTL)

### Decision & Rationale

**Final Design:**

| Component | Decision | Rationale |
|-----------|----------|-----------|
| Queue Scope | Per-subscriber | Isolates slow consumers, prevents noisy neighbor |
| Drop Policies | DROP_OLDEST (default), DROP_NEWEST, BLOCK | Different use cases need different policies |
| Block Timeout | 5s default, configurable | Prevents indefinite publisher blocking |
| TTL Enforcement | Hybrid (lazy + reaper) | Lazy check at dequeue + background reaper for memory bounds |
| TTL Priority | Message TTL > Daemon default > Infinite | Publisher control with sensible defaults |
| Client Notification | BackpressureWarning event | Subscribers aware of drops for monitoring |

**Message Invalidation Rules (in priority order):**

| Trigger | When | Metric |
|---------|------|--------|
| Delivered | gRPC Write() succeeds | `messages_delivered_total` |
| TTL Expired | `now - publish_time > ttl_ms` | `messages_expired_total` |
| DROP_OLDEST | Queue full, oldest evicted | `messages_dropped_total{reason="backpressure"}` |
| DROP_NEWEST | Queue full, new msg rejected | `messages_dropped_total{reason="backpressure"}` |
| Disconnect | Subscriber disconnects (no pause) | `messages_dropped_total{reason="disconnect"}` |

### Implementation Details

**New SubscriberQueue class:**
```cpp
class SubscriberQueue {
    size_t limit_;
    BackpressurePolicy policy_;
    std::chrono::milliseconds block_timeout_;
    std::deque<QueuedMessage> messages_;
    std::mutex mutex_;
    std::condition_variable cv_;
    
    // Metrics
    std::atomic<uint64_t> dropped_oldest_{0};
    std::atomic<uint64_t> dropped_newest_{0};
    std::atomic<uint64_t> expired_{0};
};
```

**TTL Enforcement (Hybrid approach):**
1. Lazy check: When dequeuing, skip expired messages
2. Background reaper: Periodic scan (every 1s) removes expired messages to bound memory

### Configuration

```bash
nexusd \
  # Backpressure
  --subscriber-queue-limit 10000 \
  --backpressure-policy drop-oldest \
  --block-timeout-ms 5000 \
  
  # TTL
  --default-message-ttl 60000 \
  --ttl-reaper-interval-ms 1000
```

| Option | Default | Description |
|--------|---------|-------------|
| `--subscriber-queue-limit` | 10000 | Max messages per subscriber queue |
| `--backpressure-policy` | `drop-oldest` | `drop-oldest`, `drop-newest`, `block` |
| `--block-timeout-ms` | 5000 | Timeout before fallback (block policy only) |
| `--default-message-ttl` | 0 (infinite) | Default TTL if publisher doesn't specify |
| `--ttl-reaper-interval-ms` | 1000 | How often to scan for expired messages |

### Metrics

**Prometheus metrics added:**
```
# Per-subscriber queue depth
nexusd_subscriber_queue_depth{subscription_id, topic}

# Drop counters by reason and policy
nexusd_messages_dropped_total{reason, policy, topic}

# TTL expiration counter
nexusd_messages_expired_total{topic}

# Block duration histogram
nexusd_publish_block_duration_seconds{topic}

# Queue high watermark
nexusd_subscriber_queue_high_watermark{subscription_id}
```

### Protocol Extensions

```protobuf
// New event type for subscriber notification
message BackpressureWarning {
    uint64 messages_dropped = 1;
    uint64 queue_depth = 2;
    uint64 queue_limit = 3;
    string reason = 4;  // "slow_consumer", "burst", "ttl_expired"
}

// Extended SubscribeEvent
message SubscribeEvent {
    oneof event {
        Message message = 1;
        Message retained_message = 2;
        ReplayMessage replay_message = 3;
        ReplayComplete replay_complete = 4;
        BackpressureWarning backpressure_warning = 5;  // NEW
    }
}

// Extended PublishResponse
message PublishResponse {
    bool success = 1;
    string message_id = 2;
    int32 subscriber_count = 3;
    string error_message = 4;
    int32 subscribers_dropped = 5;   // NEW: count of subscribers that dropped
    int32 subscribers_blocked = 6;   // NEW: count of subscribers that blocked
}
```

### Files Modified

- `proto/sidecar.proto` - BackpressureWarning, extended PublishResponse
- `include/nexusd/daemon/config.hpp` - Backpressure config fields
- `include/nexusd/core/subscriber_queue.hpp` - NEW: SubscriberQueue class
- `src/core/subscriber_queue.cpp` - NEW: Implementation
- `src/services/sidecar_service.cpp` - Integrate per-subscriber queues
- `src/core/CMakeLists.txt` - Add new source file

=============================================================================
## Feature: Client Resilience with Auto-Reconnection and Gap Recovery
Date: 2025-12-11 | Author: Community | Status: IMPLEMENTED
=============================================================================

### Problem Statement

When the NexusD daemon crashes or network disconnects, all 5 client libraries
(Python, Rust, C++, C#, Node.js) had NO automatic reconnection. Clients would
simply fail and require manual restart. Messages received during disconnection
were permanently lost with no way to detect or recover them.

### Design Alternatives Considered

#### Alternative 1: Client-Only Reconnection
- Clients reconnect but lose all messages during disconnect
- Simple implementation
- **Rejected**: Unacceptable message loss for critical applications

#### Alternative 2: Persistent Message Store (Kafka-style)
- All messages persisted to disk
- Clients resume from offset
- **Rejected**: Too heavy for sidecar pattern, adds latency

#### Alternative 3: Ring Buffer with Sequence Numbers ✅ SELECTED
- Server-side per-topic ring buffer stores recent messages
- Sequence numbers track message ordering
- Clients track last sequence and request replay on reconnect
- **Selected**: Good balance of reliability and performance

### Discussion Points

**Q1: How do clients detect missed messages?**
- A: Monotonically increasing sequence numbers per subscription. Gap detected when
  `received_seq > last_seq + 1`.

**Q2: What if ring buffer is smaller than gap?**
- A: Partial recovery - replay what's available, notify client of unrecoverable gap.

**Q3: Should subscriptions survive daemon restart?**
- A: No for now - subscription state is in-memory. Future: persist to disk/Redis.

**Q4: How to handle reconnection storms?**
- A: Exponential backoff with jitter prevents thundering herd.

### Decision & Rationale

**Reconnection Policy:**
- Exponential backoff: `delay = min(initial * 2^attempt, max)`
- Jitter: `delay += delay * jitter_factor * random()`
- Default: 1s initial, 30s max, 0.1 jitter, infinite retries

**Gap Recovery Modes:**
| Mode | Behavior | Use Case |
|------|----------|----------|
| `None` | Just reconnect, no recovery | Ephemeral data |
| `RetainedOnly` | Receive retained messages | Last-known-value |
| `ReplayBuffer` | Replay from ring buffer | Critical messages |

**Subscription Pause/Resume:**
- Unsubscribe with `pause=true` keeps subscription state server-side
- Resume with `last_sequence_number` triggers replay
- Paused subscriptions have TTL (default 5 minutes)

### Implementation Details

**Server-side:**
- `TopicMessageBuffer`: Per-topic ring buffer with configurable size
- `PausedSubscription`: Tracks paused subscriptions with TTL
- `ResumeSubscribe` RPC: Resumes with gap recovery

**Client-side (all 5 languages):**
- `ReconnectionPolicy`: Configurable retry parameters
- `ReconnectionState`: Connected/Disconnected/Reconnecting/Failed
- Gap detection with callback notification
- Structured logging with consistent fields

### Configuration

```bash
nexusd --message-buffer-size 1000 \
       --max-buffer-memory 104857600 \
       --paused-subscription-ttl 300000
```

### Files Modified

- `proto/sidecar.proto` - GapRecoveryMode, ResumeSubscribeRequest, sequence_number
- `include/nexusd/daemon/config.hpp` - Buffer config fields
- `include/nexusd/core/topic_message_buffer.hpp` - NEW: Ring buffer class
- `src/core/topic_message_buffer.cpp` - NEW: Implementation
- `include/nexusd/core/peer_registry.hpp` - PausedSubscription struct
- `src/core/peer_registry.cpp` - Pause/resume methods
- `src/services/sidecar_service.cpp` - ResumeSubscribe RPC
- `clients/python/nexusd_client/client.py` - Full reconnection support
- `clients/rust/src/client.rs` - ReconnectingClient wrapper
- `clients/cpp/include/nexusd_client/client.hpp` - ReconnectionPolicy
- `clients/csharp/NexusdClient/Client.cs` - Auto-reconnect
- `clients/node/src/client.ts` - Connection state management
- All client examples updated

=============================================================================
## Feature: Initial Release - Core Pub/Sub Functionality
Date: 2025-12-01 | Author: Community | Status: IMPLEMENTED
=============================================================================

### Features

- Brokerless mesh architecture with sidecar pattern
- UDP multicast peer discovery (239.255.42.1:5670)
- Hash-triggered gossip synchronization (CRC64)
- gRPC async services (SidecarService, MeshService)
- Retained messages (last-known-value per topic)
- Cross-platform support (Windows/Linux/macOS)
- Client libraries: Python, Rust, C++, C#, Node.js

### Configuration

```bash
nexusd --node-id my-node \
       --cluster-id my-cluster \
       --mesh-port 5671 \
       --sidecar-port 5672 \
       --discovery-address 239.255.42.1 \
       --discovery-port 5670
```

=============================================================================
