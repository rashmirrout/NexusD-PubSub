//! Example: Subscribing to messages with NexusD Rust client

use nexusd_client::{NexusdClient, SubscribeOptions};
use tokio_stream::StreamExt;
use tokio::signal;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Connect to daemon
    let mut client = NexusdClient::connect("http://localhost:5672").await?;
    println!("Connected to NexusD");
    println!("Subscribing to sensors/* topics...");
    println!("Press Ctrl+C to stop\n");
    
    // Subscribe to multiple topics
    let topics = vec![
        "sensors/temperature".to_string(),
        "sensors/humidity".to_string(),
        "config/settings".to_string(),
    ];
    
    let mut stream = client.subscribe(
        topics,
        SubscribeOptions {
            receive_retained: true,
            ..Default::default()
        },
    ).await?;
    
    // Process messages until Ctrl+C
    tokio::select! {
        _ = async {
            while let Some(result) = stream.next().await {
                match result {
                    Ok(msg) => {
                        let retained_flag = if msg.is_retained { " [RETAINED]" } else { "" };
                        println!("Topic: {}{}", msg.topic, retained_flag);
                        
                        match msg.payload_as_string() {
                            Ok(payload) => println!("  Payload: {}", payload),
                            Err(_) => println!("  Payload: {:?}", msg.payload),
                        }
                        
                        println!("  Message ID: {}", msg.message_id);
                        println!("  Timestamp: {}", msg.timestamp_ms);
                        println!();
                    }
                    Err(e) => {
                        eprintln!("Error: {}", e);
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
