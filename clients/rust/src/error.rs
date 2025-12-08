//! NexusD Client Error Types

use thiserror::Error;

/// Error type for NexusD client operations
#[derive(Error, Debug)]
pub enum Error {
    /// Connection error
    #[error("Connection error: {0}")]
    Connection(String),
    
    /// Publish operation failed
    #[error("Publish error: {0}")]
    Publish(String),
    
    /// Subscribe operation failed
    #[error("Subscribe error: {0}")]
    Subscribe(String),
    
    /// Unsubscribe operation failed
    #[error("Unsubscribe error: {0}")]
    Unsubscribe(String),
    
    /// gRPC transport error
    #[error("Transport error: {0}")]
    Transport(#[from] tonic::transport::Error),
    
    /// gRPC status error
    #[error("gRPC error: {0}")]
    Status(#[from] tonic::Status),
}
