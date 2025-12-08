#!/usr/bin/env python3
"""
Example: Async publish/subscribe with NexusD
"""

import asyncio
import json
from nexusd_client import AsyncNexusdClient


async def publisher(client: AsyncNexusdClient):
    """Publish messages periodically."""
    for i in range(10):
        result = await client.publish(
            topic="async/counter",
            payload=json.dumps({"count": i}).encode(),
            content_type="application/json",
        )
        print(f"Published count={i}, subscribers={result.subscriber_count}")
        await asyncio.sleep(1)


async def subscriber(client: AsyncNexusdClient):
    """Subscribe and print messages."""
    print("Subscriber started...")
    async for message in client.subscribe(["async/counter"]):
        payload = json.loads(message.payload.decode())
        print(f"  Received: {payload}")


async def main():
    async with AsyncNexusdClient("localhost:5672") as client:
        print("Connected to NexusD (async)")
        
        # Run publisher and subscriber concurrently
        await asyncio.gather(
            publisher(client),
            subscriber(client),
        )


if __name__ == "__main__":
    asyncio.run(main())
