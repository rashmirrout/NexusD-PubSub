//! Example: Publishing messages with NexusD Rust client

use nexusd_client::{NexusdClient, PublishOptions};
use std::time::Duration;
use tokio::time::sleep;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Connect to daemon
    let mut client = NexusdClient::connect("http://localhost:5672").await?;
    println!("Connected to NexusD");
    
    // Publish a simple message
    let result = client.publish(
        "sensors/temperature",
        br#"{"value": 23.5, "unit": "celsius"}"#.to_vec(),
        PublishOptions {
            content_type: "application/json".into(),
            ..Default::default()
        },
    ).await?;
    println!(
        "Published: message_id={}, subscribers={}",
        result.message_id, result.subscriber_count
    );
    
    // Publish with retain
    let result = client.publish(
        "config/settings",
        br#"{"theme": "dark", "language": "en"}"#.to_vec(),
        PublishOptions {
            content_type: "application/json".into(),
            retain: true,
            ..Default::default()
        },
    ).await?;
    println!("Published retained: message_id={}", result.message_id);
    
    // Publish multiple messages
    for i in 0..5 {
        let payload = format!(r#"{{"value": {}, "unit": "%"}}"#, 45 + i);
        let result = client.publish(
            "sensors/humidity",
            payload.into_bytes(),
            PublishOptions {
                content_type: "application/json".into(),
                ..Default::default()
            },
        ).await?;
        println!(
            "Published humidity #{}: subscribers={}",
            i, result.subscriber_count
        );
        sleep(Duration::from_millis(500)).await;
    }
    
    println!("Done!");
    Ok(())
}
