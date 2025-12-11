//! NexusD Client Implementation
//!
//! Provides an async client for the NexusD pub/sub daemon with:
//! - Automatic reconnection with exponential backoff
//! - Subscription persistence for recovery
//! - Gap detection and message replay
//! - Structured logging via tracing

use crate::error::Error;
use crate::proto::{
    self,
    sidecar_service_client::SidecarServiceClient,
    PublishRequest, SubscribeRequest, UnsubscribeRequest,
    GetTopicsRequest, GetPeersRequest, MessageEvent,
    ResumeSubscribeRequest,
};

use tokio_stream::{Stream, StreamExt};
use tonic::transport::Channel;
use std::pin::Pin;
use std::sync::Arc;
use std::time::Duration;
use tokio::sync::RwLock;
use tracing::{debug, info, warn, error};

/// Gap recovery mode for ResumeSubscribe
#[derive(Debug, Clone, Copy, Default)]
pub enum GapRecoveryMode {
    /// No gap recovery
    None = 0,
    /// Only deliver retained message
    RetainedOnly = 1,
    /// Replay all buffered messages
    #[default]
    ReplayBuffer = 2,
}

impl From<GapRecoveryMode> for i32 {
    fn from(mode: GapRecoveryMode) -> i32 {
        mode as i32
    }
}

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
    pub is_replay: bool,
    pub sequence_number: u64,
}

impl Message {
    /// Get payload as a UTF-8 string
    pub fn payload_as_string(&self) -> Result<String, std::string::FromUtf8Error> {
        String::from_utf8(self.payload.clone())
    }
}

/// Information about an active subscription
#[derive(Debug, Clone)]
pub struct SubscriptionInfo {
    pub subscription_id: String,
    pub topics: Vec<String>,
    pub gap_detected: bool,
    pub missed_message_count: u64,
    pub replay_started: bool,
    pub last_sequence: u64,
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
    pub gap_recovery_mode: GapRecoveryMode,
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

/// Configuration for automatic reconnection
#[derive(Debug, Clone)]
pub struct ReconnectConfig {
    /// Whether reconnection is enabled
    pub enabled: bool,
    /// Initial delay before first reconnection attempt
    pub initial_delay: Duration,
    /// Maximum delay between attempts
    pub max_delay: Duration,
    /// Multiplier for exponential backoff
    pub multiplier: f64,
    /// Maximum number of attempts (0 = unlimited)
    pub max_attempts: u32,
}

impl Default for ReconnectConfig {
    fn default() -> Self {
        Self {
            enabled: true,
            initial_delay: Duration::from_millis(100),
            max_delay: Duration::from_secs(30),
            multiplier: 2.0,
            max_attempts: 0,
        }
    }
}

/// Stream of messages from a subscription
pub type MessageStream = Pin<Box<dyn Stream<Item = Result<Message, Error>> + Send>>;

/// Callback for gap detection events
pub type GapCallback = Arc<dyn Fn(&SubscriptionInfo) + Send + Sync>;

/// NexusD Client
///
/// Async client for the NexusD pub/sub daemon with automatic reconnection.
///
/// # Example
///
/// ```no_run
/// use nexusd_client::{NexusdClient, PublishOptions, ReconnectConfig};
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
    addr: String,
    client: SidecarServiceClient<Channel>,
    reconnect_config: ReconnectConfig,
    active_subscriptions: Arc<RwLock<std::collections::HashMap<String, SubscriptionInfo>>>,
}

impl NexusdClient {
    /// Connect to the NexusD daemon
    ///
    /// # Arguments
    ///
    /// * `addr` - Daemon address in format "http://host:port"
    pub async fn connect(addr: &str) -> Result<Self, Error> {
        Self::connect_with_config(addr, ReconnectConfig::default()).await
    }
    
    /// Connect with custom reconnection configuration
    pub async fn connect_with_config(
        addr: &str,
        reconnect_config: ReconnectConfig,
    ) -> Result<Self, Error> {
        info!(address = addr, "Connecting to NexusD daemon");
        
        let client = SidecarServiceClient::connect(addr.to_string()).await?;
        
        info!(address = addr, "Connected to NexusD daemon");
        
        Ok(Self { 
            addr: addr.to_string(),
            client,
            reconnect_config,
            active_subscriptions: Arc::new(RwLock::new(std::collections::HashMap::new())),
        })
    }
    
    /// Attempt to reconnect with exponential backoff
    async fn reconnect(&mut self) -> Result<(), Error> {
        if !self.reconnect_config.enabled {
            return Err(Error::ConnectionFailed("Reconnection disabled".into()));
        }
        
        let mut delay = self.reconnect_config.initial_delay;
        let mut attempt = 0u32;
        
        loop {
            attempt += 1;
            
            if self.reconnect_config.max_attempts > 0 
                && attempt > self.reconnect_config.max_attempts 
            {
                error!(
                    max_attempts = self.reconnect_config.max_attempts,
                    "Max reconnection attempts exceeded"
                );
                return Err(Error::ConnectionFailed("Max reconnection attempts exceeded".into()));
            }
            
            info!(
                attempt = attempt,
                delay_ms = delay.as_millis() as u64,
                "Attempting reconnection"
            );
            
            tokio::time::sleep(delay).await;
            
            match SidecarServiceClient::connect(self.addr.clone()).await {
                Ok(client) => {
                    self.client = client;
                    info!(attempt = attempt, "Reconnection successful");
                    return Ok(());
                }
                Err(e) => {
                    warn!(
                        attempt = attempt,
                        error = %e,
                        "Reconnection attempt failed"
                    );
                }
            }
            
            delay = std::cmp::min(
                Duration::from_secs_f64(delay.as_secs_f64() * self.reconnect_config.multiplier),
                self.reconnect_config.max_delay,
            );
        }
    }
    
    /// Publish a message to a topic
    ///
    /// Automatically retries on connection failure.
    pub async fn publish(
        &mut self,
        topic: &str,
        payload: Vec<u8>,
        options: PublishOptions,
    ) -> Result<PublishResult, Error> {
        let request = PublishRequest {
            topic: topic.to_string(),
            payload: payload.clone(),
            content_type: options.content_type.clone(),
            retain: options.retain,
            correlation_id: options.correlation_id.clone(),
            ttl_ms: options.ttl_ms,
        };
        
        match self.client.publish(request.clone()).await {
            Ok(response) => {
                let resp = response.into_inner();
                debug!(
                    topic = topic,
                    message_id = resp.message_id,
                    subscriber_count = resp.subscriber_count,
                    "Message published"
                );
                Ok(PublishResult {
                    success: resp.success,
                    message_id: resp.message_id,
                    subscriber_count: resp.subscriber_count,
                    error_message: resp.error_message,
                })
            }
            Err(status) if status.code() == tonic::Code::Unavailable => {
                warn!(topic = topic, "Connection lost, attempting reconnect");
                self.reconnect().await?;
                // Retry once after reconnection
                let response = self.client.publish(request).await?.into_inner();
                Ok(PublishResult {
                    success: response.success,
                    message_id: response.message_id,
                    subscriber_count: response.subscriber_count,
                    error_message: response.error_message,
                })
            }
            Err(status) => {
                error!(topic = topic, error = %status, "Publish failed");
                Err(Error::Status(status))
            }
        }
    }
    
    /// Subscribe to topics with automatic reconnection and gap recovery
    pub async fn subscribe(
        &mut self,
        topics: Vec<String>,
        options: SubscribeOptions,
    ) -> Result<MessageStream, Error> {
        self.subscribe_with_callback(topics, options, None).await
    }
    
    /// Subscribe with a callback for gap detection events
    pub async fn subscribe_with_callback(
        &mut self,
        topics: Vec<String>,
        options: SubscribeOptions,
        on_gap_detected: Option<GapCallback>,
    ) -> Result<MessageStream, Error> {
        let request = SubscribeRequest {
            topics: topics.clone(),
            client_id: options.client_id.clone(),
            receive_retained: options.receive_retained,
            max_buffer_size: options.max_buffer_size,
        };
        
        let stream = self.client.subscribe(request).await?.into_inner();
        let subscriptions = self.active_subscriptions.clone();
        
        let message_stream = stream.filter_map(move |result| {
            let subscriptions = subscriptions.clone();
            let on_gap = on_gap_detected.clone();
            
            async move {
                match result {
                    Ok(event) => {
                        match event.event {
                            Some(proto::message_event::Event::SubscriptionInfo(info)) => {
                                let sub_info = SubscriptionInfo {
                                    subscription_id: info.subscription_id.clone(),
                                    topics: info.topics.clone(),
                                    gap_detected: info.gap_detected,
                                    missed_message_count: info.missed_message_count,
                                    replay_started: info.replay_started,
                                    last_sequence: 0,
                                };
                                
                                info!(
                                    subscription_id = info.subscription_id,
                                    topics = ?info.topics,
                                    gap_detected = info.gap_detected,
                                    missed_messages = info.missed_message_count,
                                    "Subscription active"
                                );
                                
                                if info.gap_detected {
                                    if let Some(callback) = on_gap {
                                        callback(&sub_info);
                                    }
                                }
                                
                                subscriptions.blocking_write().insert(
                                    info.subscription_id.clone(),
                                    sub_info,
                                );
                                
                                None
                            }
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
                                    is_replay: false,
                                    sequence_number: m.sequence_number,
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
                                    is_replay: false,
                                    sequence_number: 0,
                                }))
                            }
                            Some(proto::message_event::Event::ReplayMessage(m)) => {
                                Some(Ok(Message {
                                    message_id: m.message_id,
                                    topic: m.topic,
                                    payload: m.payload,
                                    content_type: String::new(),
                                    timestamp_ms: m.timestamp_ms,
                                    source_node_id: m.source_node_id,
                                    correlation_id: String::new(),
                                    is_retained: false,
                                    is_replay: true,
                                    sequence_number: m.sequence_number,
                                }))
                            }
                            Some(proto::message_event::Event::ReplayComplete(_)) => {
                                debug!("Replay complete");
                                None
                            }
                            _ => None,
                        }
                    }
                    Err(status) => Some(Err(Error::Status(status))),
                }
            }
        });
        
        Ok(Box::pin(message_stream))
    }
    
    /// Unsubscribe from a subscription
    ///
    /// # Arguments
    ///
    /// * `subscription_id` - The subscription ID to cancel
    /// * `pause` - If true, pause for later resumption instead of full cancel
    pub async fn unsubscribe(&mut self, subscription_id: &str, pause: bool) -> Result<bool, Error> {
        let request = UnsubscribeRequest {
            subscription_id: subscription_id.to_string(),
            pause,
        };
        
        let response = self.client.unsubscribe(request).await?.into_inner();
        
        // Remove from active subscriptions
        self.active_subscriptions.write().await.remove(subscription_id);
        
        info!(
            subscription_id = subscription_id,
            pause = pause,
            "Unsubscribed"
        );
        
        Ok(response.success)
    }
    
    /// Resume a paused subscription with gap recovery
    pub async fn resume_subscribe(
        &mut self,
        subscription_id: &str,
        last_sequence: u64,
        gap_recovery_mode: GapRecoveryMode,
    ) -> Result<MessageStream, Error> {
        let request = ResumeSubscribeRequest {
            subscription_id: subscription_id.to_string(),
            last_sequence_number: last_sequence,
            gap_recovery_mode: gap_recovery_mode.into(),
        };
        
        info!(
            subscription_id = subscription_id,
            last_sequence = last_sequence,
            "Resuming subscription"
        );
        
        let stream = self.client.resume_subscribe(request).await?.into_inner();
        let subscriptions = self.active_subscriptions.clone();
        
        let message_stream = stream.filter_map(move |result| {
            let subscriptions = subscriptions.clone();
            
            async move {
                match result {
                    Ok(event) => {
                        match event.event {
                            Some(proto::message_event::Event::SubscriptionInfo(info)) => {
                                let sub_info = SubscriptionInfo {
                                    subscription_id: info.subscription_id.clone(),
                                    topics: info.topics.clone(),
                                    gap_detected: info.gap_detected,
                                    missed_message_count: info.missed_message_count,
                                    replay_started: info.replay_started,
                                    last_sequence: 0,
                                };
                                
                                info!(
                                    subscription_id = info.subscription_id,
                                    gap_detected = info.gap_detected,
                                    missed_messages = info.missed_message_count,
                                    "Subscription resumed"
                                );
                                
                                subscriptions.blocking_write().insert(
                                    info.subscription_id.clone(),
                                    sub_info,
                                );
                                
                                None
                            }
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
                                    is_replay: false,
                                    sequence_number: m.sequence_number,
                                }))
                            }
                            Some(proto::message_event::Event::ReplayMessage(m)) => {
                                Some(Ok(Message {
                                    message_id: m.message_id,
                                    topic: m.topic,
                                    payload: m.payload,
                                    content_type: String::new(),
                                    timestamp_ms: m.timestamp_ms,
                                    source_node_id: m.source_node_id,
                                    correlation_id: String::new(),
                                    is_retained: false,
                                    is_replay: true,
                                    sequence_number: m.sequence_number,
                                }))
                            }
                            Some(proto::message_event::Event::ReplayComplete(_)) => {
                                debug!("Replay complete");
                                None
                            }
                            _ => None,
                        }
                    }
                    Err(status) => Some(Err(Error::Status(status))),
                }
            }
        });
        
        Ok(Box::pin(message_stream))
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
    
    /// Get active subscriptions
    pub async fn get_active_subscriptions(&self) -> Vec<SubscriptionInfo> {
        self.active_subscriptions.read().await.values().cloned().collect()
    }
}
