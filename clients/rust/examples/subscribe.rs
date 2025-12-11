//! Example: Subscribing to messages with NexusD Rust client
//! 
//! Demonstrates auto-reconnection with exponential backoff and gap detection.

use nexusd_client::{
    ReconnectingClient,
    ReconnectionPolicy,
    GapRecoveryMode,
    ReconnectionState,
};
use tokio_stream::StreamExt;
use tokio::signal;
use tracing::{info, warn, Level};
use tracing_subscriber;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Initialize tracing for structured logging
    tracing_subscriber::fmt()
        .with_max_level(Level::INFO)
        .init();

    // Configure reconnection policy
    let policy = ReconnectionPolicy {
        max_retries: None,  // None = infinite retries
        initial_delay: std::time::Duration::from_secs(1),
        max_delay: std::time::Duration::from_secs(30),
        exponential_backoff: true,
        jitter_factor: 0.1,
    };

    // Connect to daemon with resilience settings
    let mut client = ReconnectingClient::connect(
        "http://localhost:5672",
        policy,
    ).await?;
    
    info!("Connected to NexusD");
    println!("Subscribing to sensors/* topics...");
    println!("Auto-reconnect enabled with gap recovery");
    println!("Press Ctrl+C to stop\n");

    // Subscribe to multiple topics with gap recovery
    let topics = vec![
        "sensors/temperature".to_string(),
        "sensors/humidity".to_string(),
        "config/settings".to_string(),
    ];

    let mut stream = client.subscribe_with_reconnect(
        topics,
        GapRecoveryMode::ReplayBuffer,
    ).await?;

    // Process messages until Ctrl+C
    tokio::select! {
        _ = async {
            let mut last_seq: u64 = 0;
            
            while let Some(result) = stream.next().await {
                match result {
                    Ok(msg) => {
                        // Gap detection
                        if last_seq > 0 && msg.sequence_number > last_seq + 1 {
                            let gap = msg.sequence_number - last_seq - 1;
                            warn!(
                                topic = %msg.topic,
                                expected = last_seq + 1,
                                actual = msg.sequence_number,
                                gap = gap,
                                "Gap detected - missed {} messages",
                                gap
                            );
                        }
                        last_seq = msg.sequence_number;

                        let retained_flag = if msg.is_retained { " [RETAINED]" } else { "" };
                        println!("Topic: {}{}", msg.topic, retained_flag);

                        match msg.payload_as_string() {
                            Ok(payload) => println!("  Payload: {}", payload),
                            Err(_) => println!("  Payload: {:?}", msg.payload),
                        }

                        println!("  Sequence: {}", msg.sequence_number);
                        println!("  Message ID: {}", msg.message_id);
                        println!("  Timestamp: {}", msg.timestamp_ms);
                        println!();
                    }
                    Err(e) => {
                        warn!("Stream error: {}", e);
                    }
                }
            }
        } => {}
        _ = signal::ctrl_c() => {
            println!("\nShutting down...");
        }
    }

    Ok(())
}
