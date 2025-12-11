#!/usr/bin/env python3
"""
Example: Subscribing to messages from NexusD with auto-reconnection and gap detection
"""

import json
import signal
import sys
from nexusd_client import (
    NexusdClient,
    ReconnectionPolicy,
    GapRecoveryMode,
    ReconnectionState,
)

# Handle Ctrl+C gracefully
def signal_handler(sig, frame):
    print("\nShutting down...")
    sys.exit(0)

signal.signal(signal.SIGINT, signal_handler)


def main():
    # Configure reconnection policy
    policy = ReconnectionPolicy(
        max_retries=0,  # 0 = infinite retries
        initial_delay_ms=1000,
        max_delay_ms=30000,
        exponential_backoff=True,
        jitter_factor=0.1,
    )

    # Connect to the daemon with resilience settings
    with NexusdClient(
        "localhost:5672",
        reconnection_policy=policy,
        gap_recovery_mode=GapRecoveryMode.REPLAY_BUFFER,
    ) as client:
        print("Connected to NexusD")
        print("Subscribing to sensors/* topics...")
        print("Auto-reconnect enabled with gap recovery")
        print("Press Ctrl+C to stop\n")

        # Register connection state callback
        def on_state_change(state: ReconnectionState, error: Exception | None):
            if state == ReconnectionState.CONNECTED:
                print("[Connection] Connected to daemon")
            elif state == ReconnectionState.RECONNECTING:
                print(f"[Connection] Reconnecting... ({error})")
            elif state == ReconnectionState.DISCONNECTED:
                print("[Connection] Disconnected")
            elif state == ReconnectionState.FAILED:
                print(f"[Connection] Failed: {error}")

        client.on_connection_state_change(on_state_change)

        # Register gap detection callback
        def on_gap(expected_seq: int, actual_seq: int, topic: str):
            gap = actual_seq - expected_seq
            print(f"[Gap] Detected {gap} missed messages on {topic}")
            print(f"       Expected seq {expected_seq}, got {actual_seq}")

        client.on_gap_detected(on_gap)

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

            # Print message details with sequence number
            retained_flag = " [RETAINED]" if message.is_retained else ""
            print(f"Topic: {message.topic}{retained_flag}")
            print(f"  Payload: {payload}")
            print(f"  Sequence: {message.sequence_number}")
            print(f"  Message ID: {message.message_id}")
            print(f"  Timestamp: {message.timestamp_ms}")
            print()


if __name__ == "__main__":
    main()
