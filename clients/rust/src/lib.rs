//! NexusD Rust Client Library
//!
//! Async Rust client for the NexusD pub/sub sidecar daemon.
//! Features automatic reconnection, gap detection, and message replay.
//!
//! # Example
//!
//! ```no_run
//! use nexusd_client::{NexusdClient, PublishOptions, ReconnectConfig};
//!
//! #[tokio::main]
//! async fn main() -> Result<(), Box<dyn std::error::Error>> {
//!     let mut client = NexusdClient::connect("http://localhost:5672").await?;
//!     
//!     client.publish("topic", b"hello".to_vec(), Default::default()).await?;
//!     
//!     Ok(())
//! }
//! ```

mod client;
mod error;

pub mod proto {
    tonic::include_proto!("nexusd.sidecar");
}

pub use client::{
    NexusdClient,
    Message,
    PublishResult,
    PublishOptions,
    SubscribeOptions,
    TopicInfo,
    PeerInfo,
    MessageStream,
    // New types for resilience
    SubscriptionInfo,
    ReconnectConfig,
    GapRecoveryMode,
    GapCallback,
};

pub use error::Error;
