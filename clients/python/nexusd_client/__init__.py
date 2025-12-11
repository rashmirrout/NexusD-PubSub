"""
NexusD Python Client

A Python client library for the NexusD pub/sub sidecar daemon.
Features automatic reconnection, gap detection, and message replay.
"""

from nexusd_client.client import (
    NexusdClient,
    AsyncNexusdClient,
    Message,
    PublishResult,
    SubscriptionInfo,
    TopicInfo,
    PeerInfo,
    ReconnectConfig,
    GapRecoveryMode,
)
from nexusd_client.exceptions import (
    NexusdError,
    ConnectionError,
    PublishError,
    SubscribeError,
)

__version__ = "1.1.0"
__all__ = [
    # Clients
    "NexusdClient",
    "AsyncNexusdClient",
    # Data types
    "Message",
    "PublishResult",
    "SubscriptionInfo",
    "TopicInfo",
    "PeerInfo",
    # Configuration
    "ReconnectConfig",
    "GapRecoveryMode",
    # Exceptions
    "NexusdError",
    "ConnectionError",
    "PublishError",
    "SubscribeError",
]
