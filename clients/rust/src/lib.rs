//! NexusD Rust Client Library
//!
//! Async Rust client for the NexusD pub/sub sidecar daemon.
//!
//! # Example
//!
//! ```no_run
//! use nexusd_client::{NexusdClient, PublishOptions};
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
};

pub use error::Error;
