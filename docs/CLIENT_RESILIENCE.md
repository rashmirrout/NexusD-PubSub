# Client Resilience and Gap Recovery

This document describes the resilience features implemented in NexusD clients for handling daemon crashes, network disconnections, and message gap detection.

## Overview

NexusD clients now include comprehensive resilience features:

1. **Automatic Reconnection** - Clients automatically reconnect when the daemon connection is lost
2. **Gap Detection** - Sequence numbers track message ordering and detect missed messages
3. **Subscription Resumption** - Subscriptions can be paused and resumed with gap recovery
4. **Structured Logging** - Consistent logging across all client libraries

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         Client Application                       │
├─────────────────────────────────────────────────────────────────┤
│                         NexusD Client SDK                        │
│  ┌─────────────┐  ┌──────────────┐  ┌────────────────────────┐  │
│  │ Reconnection │  │ Gap Detection │  │ Subscription Manager │  │
│  │   Policy     │  │  (seq nums)   │  │  (pause/resume)      │  │
│  └─────────────┘  └──────────────┘  └────────────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│                          gRPC Channel                            │
└─────────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────┐
│                         NexusD Daemon                            │
│  ┌─────────────────┐  ┌────────────────┐  ┌──────────────────┐  │
│  │ Sidecar Service │  │ TopicMessage   │  │ Paused           │  │
│  │ (Subscribe/     │  │ Buffer         │  │ Subscriptions    │  │
│  │  ResumeSubscribe│  │ (ring buffer)  │  │ (PeerRegistry)   │  │
│  └─────────────────┘  └────────────────┘  └──────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

## Reconnection Policy

All clients support configurable reconnection with exponential backoff:

| Parameter | Default | Description |
|-----------|---------|-------------|
| `max_retries` | 0 (infinite) | Maximum reconnection attempts |
| `initial_delay_ms` | 1000 | First retry delay |
| `max_delay_ms` | 30000 | Maximum retry delay |
| `exponential_backoff` | true | Use exponential backoff |
| `jitter_factor` | 0.1 | Random jitter (0.0-1.0) |

### Delay Calculation

```
delay = min(initial_delay * 2^attempt, max_delay)
jitter = delay * jitter_factor * random()
final_delay = delay + jitter
```

## Gap Recovery Modes

Three modes are available for handling missed messages during reconnection:

### GapRecoveryMode.None
- Default mode
- No attempt to recover missed messages
- Subscription resumes with new messages only

### GapRecoveryMode.RetainedOnly
- On reconnect, receive retained messages
- Useful for "last known value" scenarios
- Low overhead

### GapRecoveryMode.ReplayBuffer
- On reconnect, replay messages from daemon's ring buffer
- Requests messages starting from last sequence number
- Best for critical message delivery

## Sequence Numbers

Every message now includes a monotonically increasing sequence number:

```protobuf
message Message {
    // ... other fields ...
    uint64 sequence_number = 8;
}
```

Clients track the last received sequence number per subscription and detect gaps when:
```
received_seq > last_seq + 1
```

Gap detection triggers:
1. Warning log message
2. Optional callback notification
3. Gap recovery if mode is ReplayBuffer

## Protocol Extensions

### ResumeSubscribeRequest

New RPC for resuming subscriptions with gap recovery:

```protobuf
message ResumeSubscribeRequest {
    string subscription_id = 1;
    repeated string topics = 2;
    string client_id = 3;
    uint64 last_sequence_number = 4;
    GapRecoveryMode gap_recovery_mode = 5;
}
```

### UnsubscribeRequest Extension

Unsubscribe now supports pausing:

```protobuf
message UnsubscribeRequest {
    string subscription_id = 1;
    bool pause = 2;  // If true, subscription can be resumed
}
```

### SubscribeEvent Extensions

New event types for replay:

```protobuf
message SubscribeEvent {
    oneof event {
        Message message = 1;
        Message retained_message = 2;
        ReplayMessage replay_message = 3;      // Replayed from buffer
        ReplayComplete replay_complete = 4;     // Replay finished
    }
}
```

## Client Usage Examples

### Python

```python
from nexusd_client import (
    NexusdClient,
    ReconnectionPolicy,
    GapRecoveryMode,
)

# Configure reconnection
policy = ReconnectionPolicy(
    max_retries=10,
    initial_delay_ms=1000,
    max_delay_ms=30000,
)

client = NexusdClient(
    "localhost:5672",
    reconnection_policy=policy,
    gap_recovery_mode=GapRecoveryMode.REPLAY_BUFFER,
)

# Register callbacks
def on_state_change(state, error):
    print(f"Connection: {state}, error: {error}")

client.on_connection_state_change(on_state_change)

def on_gap(expected, actual, topic):
    print(f"Gap detected: expected {expected}, got {actual}")

client.on_gap_detected(on_gap)

# Subscribe with auto-reconnect
for msg in client.subscribe(["my-topic"]):
    print(f"[{msg.sequence_number}] {msg.topic}: {msg.payload_as_string()}")
```

### Rust

```rust
use nexusd_client::{
    ReconnectingClient,
    ReconnectionPolicy,
    GapRecoveryMode,
};

#[tokio::main]
async fn main() {
    // Create client with reconnection
    let mut client = ReconnectingClient::connect(
        "http://localhost:5672",
        ReconnectionPolicy {
            max_retries: Some(10),
            ..Default::default()
        },
    ).await.unwrap();

    // Subscribe with gap recovery
    let mut stream = client.subscribe_with_reconnect(
        vec!["my-topic".to_string()],
        GapRecoveryMode::ReplayBuffer,
    ).await.unwrap();

    while let Some(result) = stream.next().await {
        match result {
            Ok(msg) => println!("[{}] {}: {}", msg.sequence_number, msg.topic, msg.payload_as_string()),
            Err(e) => eprintln!("Error: {}", e),
        }
    }
}
```

### C#

```csharp
using NexusdClient;

var options = new ClientOptions
{
    MaxRetries = 10,
    InitialRetryDelayMs = 1000,
    MaxRetryDelayMs = 30000,
};

using var client = new Client("localhost:5672", options);

client.OnConnectionStateChange += (state, error) =>
{
    Console.WriteLine($"Connection: {state}, error: {error?.Message}");
};

client.OnGapDetected += (expected, actual, topic) =>
{
    Console.WriteLine($"Gap: expected {expected}, got {actual}");
};

var subOptions = new SubscribeOptions
{
    GapRecoveryMode = GapRecoveryMode.ReplayBuffer,
};

await foreach (var msg in client.SubscribeAsync(new[] { "my-topic" }, subOptions))
{
    Console.WriteLine($"[{msg.SequenceNumber}] {msg.Topic}: {msg.PayloadAsString()}");
}
```

### Node.js/TypeScript

```typescript
import { NexusdClient, GapRecoveryMode, ReconnectionState } from 'nexusd-client';

const client = new NexusdClient('localhost:5672', {
    reconnection: {
        maxRetries: 10,
        initialDelayMs: 1000,
        maxDelayMs: 30000,
    },
    gapRecoveryMode: GapRecoveryMode.ReplayBuffer,
    debug: true,
});

client.onConnectionStateChange((state, error) => {
    console.log('Connection:', state, error?.message);
});

client.onGapDetected((expected, actual, topic) => {
    console.log(`Gap: expected ${expected}, got ${actual}`);
});

for await (const msg of client.subscribe(['my-topic'])) {
    console.log(`[${msg.sequenceNumber}] ${msg.topic}: ${msg.payloadAsString()}`);
}

client.close();
```

### C++

```cpp
#include <nexusd_client/client.hpp>

int main() {
    nexusd::ReconnectionPolicy policy{
        .max_retries = 10,
        .initial_delay_ms = 1000,
        .max_delay_ms = 30000,
    };

    nexusd::NexusdClient client("localhost:5672", policy);
    
    client.setReconnectionCallback([](nexusd::ReconnectionState state, const std::string& error) {
        std::cout << "Connection: " << static_cast<int>(state) << " " << error << std::endl;
    });

    nexusd::SubscribeOptions opts;
    opts.gap_recovery_mode = nexusd::GapRecoveryMode::ReplayBuffer;

    client.subscribe(
        {"my-topic"},
        [](const nexusd::Message& msg) {
            std::cout << "[" << msg.sequence_number << "] " 
                      << msg.topic << ": " << msg.payloadAsString() << std::endl;
        },
        opts
    );

    return 0;
}
```

## Server Configuration

New daemon configuration options for message buffering:

```bash
nexusd --message-buffer-size 1000 \
       --max-buffer-memory 104857600 \
       --paused-subscription-ttl 300000
```

| Option | Default | Description |
|--------|---------|-------------|
| `--message-buffer-size` | 1000 | Messages per topic in ring buffer |
| `--max-buffer-memory` | 100MB | Max memory for all buffers |
| `--paused-subscription-ttl` | 300000ms | TTL for paused subscriptions |

## Implementation Details

### TopicMessageBuffer

Server-side ring buffer for per-topic message storage:

```cpp
class TopicMessageBuffer {
public:
    void addMessage(const std::string& topic, const Message& msg, uint64_t seq);
    std::vector<BufferedMessage> getMessagesFrom(const std::string& topic, uint64_t fromSeq);
    std::optional<uint64_t> getLatestSequence(const std::string& topic);
};
```

- Lock-free reads where possible
- Automatic eviction of old messages
- Memory-bounded storage

### PausedSubscription

Server tracks paused subscriptions for resumption:

```cpp
struct PausedSubscription {
    std::string subscription_id;
    std::vector<std::string> topics;
    std::string client_id;
    uint64_t last_sequence_number;
    std::chrono::steady_clock::time_point paused_at;
};
```

- TTL-based cleanup
- Automatic sequence tracking
- Topic list preservation

## Error Handling

### Transient Errors (Retry)
- `UNAVAILABLE` - Server unreachable
- `DEADLINE_EXCEEDED` - Timeout

### Fatal Errors (No Retry)
- `PERMISSION_DENIED` - Auth failure
- `INVALID_ARGUMENT` - Bad request
- `UNIMPLEMENTED` - Missing RPC

## Logging

All clients use structured logging with consistent fields:

| Field | Description |
|-------|-------------|
| `subscription_id` | Unique subscription identifier |
| `topics` | List of subscribed topics |
| `last_sequence_number` | Last received sequence |
| `attempt` | Retry attempt number |
| `delay_ms` | Retry delay |
| `gap` | Number of missed messages |

### Log Levels

- **DEBUG**: Message receipts, sequence numbers
- **INFO**: Connection state changes, subscription events
- **WARN**: Gaps detected, retry attempts
- **ERROR**: Unrecoverable failures

## Metrics (Future)

Planned Prometheus metrics:

```
nexusd_client_reconnections_total
nexusd_client_gaps_detected_total
nexusd_client_messages_received_total
nexusd_client_connection_state
```

## Best Practices

1. **Always set gap recovery mode** for critical message flows
2. **Monitor gap detection callbacks** to track message loss
3. **Configure reasonable retry limits** for ephemeral subscriptions
4. **Use subscription IDs** for long-lived subscriptions that need resumption
5. **Set appropriate buffer sizes** on the daemon based on message rate

## Troubleshooting

### High reconnection rate
- Check network stability
- Increase `initial_delay_ms`
- Check daemon health

### Frequent gaps
- Increase `--message-buffer-size`
- Check for slow consumers
- Monitor network latency

### Memory issues
- Reduce `--max-buffer-memory`
- Decrease buffer size per topic
- Add more daemon instances
