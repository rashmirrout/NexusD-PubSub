"""
NexusD Python Client

A Python client library for the NexusD pub/sub sidecar daemon.
"""

from nexusd_client.client import NexusdClient, AsyncNexusdClient
from nexusd_client.exceptions import (
    NexusdError,
    ConnectionError,
    PublishError,
    SubscribeError,
)

__version__ = "1.0.0"
__all__ = [
    "NexusdClient",
    "AsyncNexusdClient",
    "NexusdError",
    "ConnectionError",
    "PublishError",
    "SubscribeError",
]
