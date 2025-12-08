#!/usr/bin/env python3
"""
Example: Subscribing to messages from NexusD
"""

import json
import signal
import sys
from nexusd_client import NexusdClient

# Handle Ctrl+C gracefully
def signal_handler(sig, frame):
    print("\nShutting down...")
    sys.exit(0)

signal.signal(signal.SIGINT, signal_handler)


def main():
    # Connect to the daemon
    with NexusdClient("localhost:5672") as client:
        print("Connected to NexusD")
        print("Subscribing to sensors/* topics...")
        print("Press Ctrl+C to stop\n")
        
        # Subscribe to multiple topics
        topics = ["sensors/temperature", "sensors/humidity", "config/settings"]
        
        for message in client.subscribe(topics, receive_retained=True):
            # Parse JSON payload if applicable
            try:
                if message.content_type == "application/json":
                    payload = json.loads(message.payload.decode())
                else:
                    payload = message.payload.decode()
            except:
                payload = message.payload
            
            # Print message details
            retained_flag = " [RETAINED]" if message.is_retained else ""
            print(f"Topic: {message.topic}{retained_flag}")
            print(f"  Payload: {payload}")
            print(f"  Message ID: {message.message_id}")
            print(f"  Timestamp: {message.timestamp_ms}")
            print()


if __name__ == "__main__":
    main()
