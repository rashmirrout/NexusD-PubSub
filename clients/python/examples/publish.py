#!/usr/bin/env python3
"""
Example: Publishing messages to NexusD
"""

import json
import time
from nexusd_client import NexusdClient

def main():
    # Connect to the daemon
    with NexusdClient("localhost:5672") as client:
        print("Connected to NexusD")
        
        # Publish a simple message
        result = client.publish(
            topic="sensors/temperature",
            payload=json.dumps({"value": 23.5, "unit": "celsius"}).encode(),
            content_type="application/json",
        )
        print(f"Published: message_id={result.message_id}, subscribers={result.subscriber_count}")
        
        # Publish with retain flag
        result = client.publish(
            topic="config/settings",
            payload=json.dumps({"theme": "dark", "language": "en"}).encode(),
            content_type="application/json",
            retain=True,
        )
        print(f"Published retained: message_id={result.message_id}")
        
        # Publish multiple messages
        for i in range(5):
            result = client.publish(
                topic="sensors/humidity",
                payload=json.dumps({"value": 45 + i, "unit": "%"}).encode(),
                content_type="application/json",
            )
            print(f"Published humidity #{i}: subscribers={result.subscriber_count}")
            time.sleep(0.5)
        
        print("Done!")


if __name__ == "__main__":
    main()
