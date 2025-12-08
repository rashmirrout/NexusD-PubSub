//! NexusD Client Implementation

use crate::error::Error;
use crate::proto::{
    self,
    sidecar_service_client::SidecarServiceClient,
    PublishRequest, SubscribeRequest, UnsubscribeRequest,
    GetTopicsRequest, GetPeersRequest, MessageEvent,
};

use tokio_stream::{Stream, StreamExt};
use tonic::transport::Channel;
use std::pin::Pin;

/// A received message from a subscription
#[derive(Debug, Clone)]
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

impl Message {
    /// Get payload as a UTF-8 string
    pub fn payload_as_string(&self) -> Result<String, std::string::FromUtf8Error> {
        String::from_utf8(self.payload.clone())
    }
}

/// Result of a publish operation
#[derive(Debug, Clone)]
pub struct PublishResult {
    pub success: bool,
    pub message_id: String,
    pub subscriber_count: i32,
    pub error_message: String,
}

/// Options for publish operations
#[derive(Debug, Clone, Default)]
pub struct PublishOptions {
    pub content_type: String,
    pub retain: bool,
    pub correlation_id: String,
    pub ttl_ms: i64,
}

/// Options for subscribe operations
#[derive(Debug, Clone, Default)]
pub struct SubscribeOptions {
    pub client_id: String,
    pub receive_retained: bool,
    pub max_buffer_size: i32,
}

/// Topic information
#[derive(Debug, Clone)]
pub struct TopicInfo {
    pub topic: String,
    pub subscriber_count: i32,
    pub has_retained: bool,
}

/// Mesh peer information
#[derive(Debug, Clone)]
pub struct PeerInfo {
    pub node_id: String,
    pub address: String,
    pub cluster_id: String,
    pub is_healthy: bool,
}

/// Stream of messages from a subscription
pub type MessageStream = Pin<Box<dyn Stream<Item = Result<Message, Error>> + Send>>;

/// NexusD Client
///
/// Async client for the NexusD pub/sub daemon.
///
/// # Example
///
/// ```no_run
/// use nexusd_client::{NexusdClient, PublishOptions};
///
/// #[tokio::main]
/// async fn main() -> Result<(), Box<dyn std::error::Error>> {
///     let mut client = NexusdClient::connect("http://localhost:5672").await?;
///     
///     // Publish
///     client.publish("topic", b"hello".to_vec(), Default::default()).await?;
///     
///     Ok(())
/// }
/// ```
pub struct NexusdClient {
    client: SidecarServiceClient<Channel>,
}

impl NexusdClient {
    /// Connect to the NexusD daemon
    ///
    /// # Arguments
    ///
    /// * `addr` - Daemon address in format "http://host:port"
    ///
    /// # Example
    ///
    /// ```no_run
    /// # use nexusd_client::NexusdClient;
    /// # async fn example() -> Result<(), Box<dyn std::error::Error>> {
    /// let client = NexusdClient::connect("http://localhost:5672").await?;
    /// # Ok(())
    /// # }
    /// ```
    pub async fn connect(addr: &str) -> Result<Self, Error> {
        let client = SidecarServiceClient::connect(addr.to_string()).await?;
        Ok(Self { client })
    }
    
    /// Publish a message to a topic
    ///
    /// # Arguments
    ///
    /// * `topic` - Target topic
    /// * `payload` - Message payload as bytes
    /// * `options` - Publish options
    ///
    /// # Returns
    ///
    /// `PublishResult` with success status and metadata
    pub async fn publish(
        &mut self,
        topic: &str,
        payload: Vec<u8>,
        options: PublishOptions,
    ) -> Result<PublishResult, Error> {
        let request = PublishRequest {
            topic: topic.to_string(),
            payload,
            content_type: options.content_type,
            retain: options.retain,
            correlation_id: options.correlation_id,
            ttl_ms: options.ttl_ms,
        };
        
        let response = self.client.publish(request).await?.into_inner();
        
        Ok(PublishResult {
            success: response.success,
            message_id: response.message_id,
            subscriber_count: response.subscriber_count,
            error_message: response.error_message,
        })
    }
    
    /// Subscribe to topics
    ///
    /// Returns a stream of messages that can be iterated asynchronously.
    ///
    /// # Arguments
    ///
    /// * `topics` - List of topic names to subscribe to
    /// * `options` - Subscribe options
    ///
    /// # Returns
    ///
    /// A stream of `Message` objects
    ///
    /// # Example
    ///
    /// ```no_run
    /// # use nexusd_client::{NexusdClient, SubscribeOptions};
    /// # use tokio_stream::StreamExt;
    /// # async fn example() -> Result<(), Box<dyn std::error::Error>> {
    /// # let mut client = NexusdClient::connect("http://localhost:5672").await?;
    /// let mut stream = client.subscribe(
    ///     vec!["sensors/temp".into()],
    ///     Default::default(),
    /// ).await?;
    ///
    /// while let Some(result) = stream.next().await {
    ///     let msg = result?;
    ///     println!("{}: {:?}", msg.topic, msg.payload);
    /// }
    /// # Ok(())
    /// # }
    /// ```
    pub async fn subscribe(
        &mut self,
        topics: Vec<String>,
        options: SubscribeOptions,
    ) -> Result<MessageStream, Error> {
        let request = SubscribeRequest {
            topics,
            client_id: options.client_id,
            receive_retained: options.receive_retained,
            max_buffer_size: options.max_buffer_size,
        };
        
        let stream = self.client.subscribe(request).await?.into_inner();
        
        let message_stream = stream.filter_map(|result| {
            match result {
                Ok(event) => {
                    // Convert MessageEvent to Message
                    match event.event {
                        Some(proto::message_event::Event::Message(m)) => {
                            Some(Ok(Message {
                                message_id: m.message_id,
                                topic: m.topic,
                                payload: m.payload,
                                content_type: m.content_type,
                                timestamp_ms: m.timestamp_ms,
                                source_node_id: m.source_node_id,
                                correlation_id: m.correlation_id,
                                is_retained: false,
                            }))
                        }
                        Some(proto::message_event::Event::RetainedMessage(m)) => {
                            Some(Ok(Message {
                                message_id: m.message_id,
                                topic: m.topic,
                                payload: m.payload,
                                content_type: m.content_type,
                                timestamp_ms: m.timestamp_ms,
                                source_node_id: m.source_node_id,
                                correlation_id: m.correlation_id,
                                is_retained: true,
                            }))
                        }
                        // Ignore subscription_info and heartbeat
                        _ => None,
                    }
                }
                Err(status) => Some(Err(Error::Status(status))),
            }
        });
        
        Ok(Box::pin(message_stream))
    }
    
    /// Unsubscribe from a subscription
    ///
    /// # Arguments
    ///
    /// * `subscription_id` - The subscription ID to cancel
    ///
    /// # Returns
    ///
    /// `true` if unsubscribed successfully
    pub async fn unsubscribe(&mut self, subscription_id: &str) -> Result<bool, Error> {
        let request = UnsubscribeRequest {
            subscription_id: subscription_id.to_string(),
        };
        
        let response = self.client.unsubscribe(request).await?.into_inner();
        Ok(response.success)
    }
    
    /// Get list of topics with subscriber counts
    pub async fn get_topics(&mut self) -> Result<Vec<TopicInfo>, Error> {
        let request = GetTopicsRequest {};
        let response = self.client.get_topics(request).await?.into_inner();
        
        Ok(response.topics.into_iter().map(|t| TopicInfo {
            topic: t.topic,
            subscriber_count: t.subscriber_count,
            has_retained: t.has_retained,
        }).collect())
    }
    
    /// Get list of mesh peers
    pub async fn get_peers(&mut self) -> Result<Vec<PeerInfo>, Error> {
        let request = GetPeersRequest {};
        let response = self.client.get_peers(request).await?.into_inner();
        
        Ok(response.peers.into_iter().map(|p| PeerInfo {
            node_id: p.node_id,
            address: p.address,
            cluster_id: p.cluster_id,
            is_healthy: p.is_healthy,
        }).collect())
    }
}
