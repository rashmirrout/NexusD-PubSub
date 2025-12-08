# NexusD Rust Client

Async Rust client library for the NexusD pub/sub sidecar daemon.

## Installation

Add to your `Cargo.toml`:

```toml
[dependencies]
nexusd-client = "1.0"
tokio = { version = "1.0", features = ["full"] }
```

## Building from Source

```bash
cargo build
```

## Usage

### Basic Publish/Subscribe

```rust
use nexusd_client::{NexusdClient, PublishOptions};

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Connect to daemon
    let mut client = NexusdClient::connect("http://localhost:5672").await?;
    
    // Publish a message
    let result = client.publish(
        "sensors/temperature",
        b"{\"value\": 23.5}".to_vec(),
        PublishOptions::default(),
    ).await?;
    println!("Published to {} subscribers", result.subscriber_count);
    
    // Subscribe to topics
    let mut stream = client.subscribe(
        vec!["sensors/temperature".into()],
        Default::default(),
    ).await?;
    
    while let Some(message) = stream.next().await {
        let msg = message?;
        println!("Received: {} -> {:?}", msg.topic, msg.payload_as_string());
    }
    
    Ok(())
}
```

### With Options

```rust
// Publish with retain
client.publish(
    "config/settings",
    b"{\"theme\": \"dark\"}".to_vec(),
    PublishOptions {
        content_type: "application/json".into(),
        retain: true,
        ..Default::default()
    },
).await?;

// Subscribe with retained messages
let stream = client.subscribe(
    vec!["config/#".into()],
    SubscribeOptions {
        receive_retained: true,
        ..Default::default()
    },
).await?;
```

## API Reference

### NexusdClient

```rust
impl NexusdClient {
    pub async fn connect(addr: &str) -> Result<Self, Error>;
    
    pub async fn publish(
        &mut self,
        topic: &str,
        payload: Vec<u8>,
        options: PublishOptions,
    ) -> Result<PublishResult, Error>;
    
    pub async fn subscribe(
        &mut self,
        topics: Vec<String>,
        options: SubscribeOptions,
    ) -> Result<MessageStream, Error>;
    
    pub async fn unsubscribe(&mut self, subscription_id: &str) -> Result<bool, Error>;
    
    pub async fn get_topics(&mut self) -> Result<Vec<TopicInfo>, Error>;
    
    pub async fn get_peers(&mut self) -> Result<Vec<PeerInfo>, Error>;
}
```

### Types

```rust
pub struct Message {
    pub message_id: String,
    pub topic: String,
    pub payload: Vec<u8>,
    pub content_type: String,
    pub timestamp_ms: i64,
    pub source_node_id: String,
    pub correlation_id: String,
    pub is_retained: bool,
}

pub struct PublishResult {
    pub success: bool,
    pub message_id: String,
    pub subscriber_count: i32,
    pub error_message: String,
}
```

## Requirements

- Rust 1.70+
- NexusD daemon running on target address
