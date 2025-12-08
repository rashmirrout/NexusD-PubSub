# NexusD Python Client

Python client library for the NexusD pub/sub sidecar daemon.

## Installation

```bash
# From source
pip install -e .

# Or with uv
uv pip install -e .
```

## Generate Proto (Development)

```bash
# Install dev dependencies
pip install -e ".[dev]"

# Generate proto files
python -m grpc_tools.protoc \
    -I../../proto \
    --python_out=nexusd_client/generated \
    --grpc_python_out=nexusd_client/generated \
    ../../proto/sidecar.proto
```

## Usage

### Basic Publish/Subscribe

```python
from nexusd_client import NexusdClient

# Connect to daemon
client = NexusdClient("localhost:5672")

# Publish a message
response = client.publish(
    topic="sensors/temperature",
    payload=b'{"value": 23.5}',
    content_type="application/json"
)
print(f"Published to {response.subscriber_count} subscribers")

# Subscribe to topics
for message in client.subscribe(["sensors/temperature", "sensors/humidity"]):
    print(f"Received: {message.topic} -> {message.payload}")

# Close connection
client.close()
```

### Async Usage

```python
import asyncio
from nexusd_client import AsyncNexusdClient

async def main():
    async with AsyncNexusdClient("localhost:5672") as client:
        # Publish
        await client.publish("sensors/temp", b'23.5')
        
        # Subscribe
        async for message in client.subscribe(["sensors/#"]):
            print(f"{message.topic}: {message.payload}")

asyncio.run(main())
```

### With Retained Messages

```python
# Publish with retain flag
client.publish(
    topic="config/settings",
    payload=b'{"theme": "dark"}',
    retain=True
)

# New subscribers receive retained message immediately
for message in client.subscribe(["config/settings"], receive_retained=True):
    print(message)
```

## API Reference

### NexusdClient

| Method | Description |
|--------|-------------|
| `publish(topic, payload, ...)` | Publish a message to a topic |
| `subscribe(topics, ...)` | Subscribe and iterate over messages |
| `unsubscribe(subscription_id)` | Cancel a subscription |
| `get_topics()` | List topics with subscriber counts |
| `get_peers()` | List mesh peers |
| `close()` | Close the connection |

### AsyncNexusdClient

Same API but all methods are `async` and `subscribe()` returns an async iterator.

## Requirements

- Python 3.9+
- NexusD daemon running on target address
