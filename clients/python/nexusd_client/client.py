"""
NexusD Client Implementation

Provides synchronous and asynchronous clients for the NexusD pub/sub daemon.
Supports automatic reconnection, gap detection, and subscription recovery.
"""

from __future__ import annotations

import uuid
import time
import logging
import threading
from typing import Iterator, AsyncIterator, Optional, List, Callable, Dict
from dataclasses import dataclass, field
from enum import Enum

import grpc
import grpc.aio

# Import generated proto modules
from nexusd_client.generated import sidecar_pb2
from nexusd_client.generated import sidecar_pb2_grpc

from nexusd_client.exceptions import (
    ConnectionError,
    PublishError,
    SubscribeError,
    UnsubscribeError,
)

# Configure client logger
logger = logging.getLogger("nexusd_client")


class GapRecoveryMode(Enum):
    """Gap recovery mode for ResumeSubscribe."""
    NONE = 0
    RETAINED_ONLY = 1
    REPLAY_BUFFER = 2


@dataclass
class Message:
    """Represents a received message."""
    message_id: str
    topic: str
    payload: bytes
    content_type: str
    timestamp_ms: int
    source_node_id: str
    correlation_id: str
    is_retained: bool = False
    is_replay: bool = False
    sequence_number: int = 0


@dataclass
class SubscriptionInfo:
    """Information about an active subscription."""
    subscription_id: str
    topics: List[str]
    gap_detected: bool = False
    missed_message_count: int = 0
    replay_started: bool = False
    last_sequence: int = 0


@dataclass
class PublishResult:
    """Result of a publish operation."""
    success: bool
    message_id: str
    subscriber_count: int
    error_message: str = ""


@dataclass
class TopicInfo:
    """Information about a topic."""
    topic: str
    subscriber_count: int
    has_retained: bool


@dataclass
class PeerInfo:
    """Information about a mesh peer."""
    node_id: str
    address: str
    cluster_id: str
    is_healthy: bool


@dataclass
class ReconnectConfig:
    """Configuration for automatic reconnection."""
    enabled: bool = True
    initial_delay_ms: int = 100
    max_delay_ms: int = 30000
    multiplier: float = 2.0
    max_attempts: int = 0  # 0 = unlimited
    jitter: float = 0.1


class NexusdClient:
    """
    Synchronous client for NexusD pub/sub daemon.
    
    Features:
    - Automatic reconnection with exponential backoff
    - Subscription persistence for recovery
    - Gap detection and message replay
    - Structured logging
    
    Example:
        client = NexusdClient("localhost:5672")
        client.publish("topic", b"hello")
        for msg in client.subscribe(["topic"]):
            print(msg)
        client.close()
    """
    
    def __init__(
        self, 
        address: str = "localhost:5672",
        reconnect_config: Optional[ReconnectConfig] = None,
    ):
        """
        Initialize the client.
        
        Args:
            address: Daemon address in "host:port" format
            reconnect_config: Reconnection configuration (default: enabled)
        """
        self._address = address
        self._channel: Optional[grpc.Channel] = None
        self._stub: Optional[sidecar_pb2_grpc.SidecarServiceStub] = None
        self._client_id = str(uuid.uuid4())
        self._reconnect_config = reconnect_config or ReconnectConfig()
        
        # Subscription state for recovery
        self._active_subscriptions: Dict[str, SubscriptionInfo] = {}
        self._subscription_lock = threading.Lock()
        
        logger.info("NexusdClient initialized", extra={
            "address": address,
            "client_id": self._client_id,
            "reconnect_enabled": self._reconnect_config.enabled,
        })
        
        self._connect()
    
    def _connect(self) -> None:
        """Establish connection to the daemon."""
        try:
            self._channel = grpc.insecure_channel(self._address)
            self._stub = sidecar_pb2_grpc.SidecarServiceStub(self._channel)
            logger.info("Connected to daemon", extra={"address": self._address})
        except Exception as e:
            logger.error("Connection failed", extra={
                "address": self._address,
                "error": str(e),
            })
            raise ConnectionError(f"Failed to connect to {self._address}: {e}")
    
    def _reconnect(self) -> bool:
        """
        Attempt to reconnect with exponential backoff.
        
        Returns:
            True if reconnection succeeded
        """
        if not self._reconnect_config.enabled:
            return False
        
        delay = self._reconnect_config.initial_delay_ms / 1000.0
        attempt = 0
        
        while True:
            attempt += 1
            if (self._reconnect_config.max_attempts > 0 and 
                attempt > self._reconnect_config.max_attempts):
                logger.error("Max reconnection attempts exceeded", extra={
                    "max_attempts": self._reconnect_config.max_attempts,
                })
                return False
            
            logger.info("Attempting reconnection", extra={
                "attempt": attempt,
                "delay_ms": int(delay * 1000),
            })
            
            time.sleep(delay)
            
            try:
                self._connect()
                logger.info("Reconnection successful", extra={"attempt": attempt})
                return True
            except Exception as e:
                logger.warning("Reconnection attempt failed", extra={
                    "attempt": attempt,
                    "error": str(e),
                })
            
            # Exponential backoff with jitter
            delay = min(
                delay * self._reconnect_config.multiplier,
                self._reconnect_config.max_delay_ms / 1000.0
            )
            import random
            jitter = delay * self._reconnect_config.jitter * (2 * random.random() - 1)
            delay += jitter
    
    def close(self) -> None:
        """Close the connection."""
        if self._channel:
            # Pause subscriptions for potential recovery
            with self._subscription_lock:
                for sub_id in list(self._active_subscriptions.keys()):
                    try:
                        self._pause_subscription(sub_id)
                    except Exception:
                        pass
            
            self._channel.close()
            self._channel = None
            self._stub = None
            logger.info("Connection closed", extra={"address": self._address})
    
    def __enter__(self) -> "NexusdClient":
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        self.close()
    
    def _pause_subscription(self, subscription_id: str) -> bool:
        """Pause a subscription for later recovery."""
        if not self._stub:
            return False
        
        try:
            request = sidecar_pb2.UnsubscribeRequest(
                subscription_id=subscription_id,
                pause=True,
            )
            self._stub.Unsubscribe(request)
            logger.debug("Subscription paused", extra={
                "subscription_id": subscription_id,
            })
            return True
        except Exception as e:
            logger.warning("Failed to pause subscription", extra={
                "subscription_id": subscription_id,
                "error": str(e),
            })
            return False
    
    def publish(
        self,
        topic: str,
        payload: bytes,
        content_type: str = "",
        retain: bool = False,
        correlation_id: str = "",
        ttl_ms: int = 0,
    ) -> PublishResult:
        """
        Publish a message to a topic.
        
        Args:
            topic: Target topic
            payload: Message payload as bytes
            content_type: Optional content type hint
            retain: If True, message is retained for late subscribers
            correlation_id: Optional correlation ID for request-response
            ttl_ms: Time-to-live in milliseconds (0 = no expiration)
        
        Returns:
            PublishResult with success status and metadata
        
        Raises:
            PublishError: If publish fails after reconnection attempts
        """
        if not self._stub:
            raise ConnectionError("Not connected")
        
        request = sidecar_pb2.PublishRequest(
            topic=topic,
            payload=payload,
            content_type=content_type,
            retain=retain,
            correlation_id=correlation_id,
            ttl_ms=ttl_ms,
        )
        
        try:
            response = self._stub.Publish(request)
            logger.debug("Message published", extra={
                "topic": topic,
                "message_id": response.message_id,
                "subscriber_count": response.subscriber_count,
            })
            return PublishResult(
                success=response.success,
                message_id=response.message_id,
                subscriber_count=response.subscriber_count,
                error_message=response.error_message,
            )
        except grpc.RpcError as e:
            if e.code() == grpc.StatusCode.UNAVAILABLE and self._reconnect():
                # Retry after reconnection
                return self.publish(topic, payload, content_type, retain, 
                                   correlation_id, ttl_ms)
            logger.error("Publish failed", extra={
                "topic": topic,
                "error": str(e),
            })
            raise PublishError(f"Publish failed: {e.details()}")
        try:
            response = self._stub.Publish(request)
            return PublishResult(
                success=response.success,
                message_id=response.message_id,
                subscriber_count=response.subscriber_count,
                error_message=response.error_message,
            )
        except grpc.RpcError as e:
            raise PublishError(f"Publish failed: {e.details()}")
    
    def subscribe(
        self,
        topics: List[str],
        receive_retained: bool = True,
        max_buffer_size: int = 0,
        gap_recovery_mode: GapRecoveryMode = GapRecoveryMode.REPLAY_BUFFER,
        on_gap_detected: Optional[Callable[[SubscriptionInfo], None]] = None,
    ) -> Iterator[Message]:
        """
        Subscribe to topics and yield messages.
        
        Features automatic reconnection and gap recovery.
        
        Args:
            topics: List of topic names to subscribe to
            receive_retained: If True, receive retained messages
            max_buffer_size: Max messages to buffer (0 = unlimited)
            gap_recovery_mode: How to handle gaps after reconnection
            on_gap_detected: Callback when gap is detected
        
        Yields:
            Message objects as they arrive
        
        Raises:
            SubscribeError: If subscription fails permanently
        """
        if not self._stub:
            raise ConnectionError("Not connected")
        
        subscription_id: Optional[str] = None
        last_sequence: int = 0
        
        while True:
            try:
                if subscription_id and self._reconnect_config.enabled:
                    # Attempt to resume with gap recovery
                    logger.info("Resuming subscription after reconnection", extra={
                        "subscription_id": subscription_id,
                        "last_sequence": last_sequence,
                    })
                    
                    request = sidecar_pb2.ResumeSubscribeRequest(
                        subscription_id=subscription_id,
                        last_sequence_number=last_sequence,
                        gap_recovery_mode=gap_recovery_mode.value,
                    )
                    stream = self._stub.ResumeSubscribe(request)
                else:
                    # Fresh subscription
                    request = sidecar_pb2.SubscribeRequest(
                        topics=topics,
                        client_id=self._client_id,
                        receive_retained=receive_retained,
                        max_buffer_size=max_buffer_size,
                    )
                    stream = self._stub.Subscribe(request)
                
                for event in stream:
                    event_type = event.WhichOneof("event")
                    
                    if event_type == "subscription_info":
                        info = event.subscription_info
                        subscription_id = info.subscription_id
                        
                        sub_info = SubscriptionInfo(
                            subscription_id=info.subscription_id,
                            topics=list(info.topics),
                            gap_detected=info.gap_detected,
                            missed_message_count=info.missed_message_count,
                            replay_started=info.replay_started,
                            last_sequence=last_sequence,
                        )
                        
                        with self._subscription_lock:
                            self._active_subscriptions[subscription_id] = sub_info
                        
                        logger.info("Subscription active", extra={
                            "subscription_id": subscription_id,
                            "topics": list(info.topics),
                            "gap_detected": info.gap_detected,
                            "missed_messages": info.missed_message_count,
                        })
                        
                        if info.gap_detected and on_gap_detected:
                            on_gap_detected(sub_info)
                    
                    elif event_type == "message":
                        msg = event.message
                        last_sequence = msg.sequence_number or last_sequence
                        
                        yield Message(
                            message_id=msg.message_id,
                            topic=msg.topic,
                            payload=msg.payload,
                            content_type=msg.content_type,
                            timestamp_ms=msg.timestamp_ms,
                            source_node_id=msg.source_node_id,
                            correlation_id=msg.correlation_id,
                            is_retained=False,
                            sequence_number=msg.sequence_number,
                        )
                    
                    elif event_type == "retained_message":
                        msg = event.retained_message
                        yield Message(
                            message_id=msg.message_id,
                            topic=msg.topic,
                            payload=msg.payload,
                            content_type=msg.content_type,
                            timestamp_ms=msg.timestamp_ms,
                            source_node_id=msg.source_node_id,
                            correlation_id=msg.correlation_id,
                            is_retained=True,
                        )
                    
                    elif event_type == "replay_message":
                        msg = event.replay_message
                        last_sequence = max(last_sequence, msg.sequence_number)
                        
                        yield Message(
                            message_id=msg.message_id,
                            topic=msg.topic,
                            payload=msg.payload,
                            content_type="",
                            timestamp_ms=msg.timestamp_ms,
                            source_node_id=msg.source_node_id,
                            correlation_id="",
                            is_retained=False,
                            is_replay=True,
                            sequence_number=msg.sequence_number,
                        )
                    
                    elif event_type == "replay_complete":
                        logger.info("Replay complete", extra={
                            "subscription_id": subscription_id,
                        })
                    
                    # Update sequence tracking
                    if subscription_id:
                        with self._subscription_lock:
                            if subscription_id in self._active_subscriptions:
                                self._active_subscriptions[subscription_id].last_sequence = last_sequence
                
                # Stream ended normally
                break
                    
            except grpc.RpcError as e:
                if e.code() == grpc.StatusCode.CANCELLED:
                    logger.debug("Subscription cancelled", extra={
                        "subscription_id": subscription_id,
                    })
                    break
                
                if e.code() == grpc.StatusCode.UNAVAILABLE:
                    logger.warning("Connection lost during subscription", extra={
                        "subscription_id": subscription_id,
                        "last_sequence": last_sequence,
                    })
                    
                    if self._reconnect():
                        # Will retry with ResumeSubscribe
                        continue
                
                logger.error("Subscribe failed", extra={
                    "error": str(e),
                    "code": e.code().name if hasattr(e, 'code') else "UNKNOWN",
                })
                raise SubscribeError(f"Subscribe failed: {e.details()}")
        
        # Cleanup
        if subscription_id:
            with self._subscription_lock:
                self._active_subscriptions.pop(subscription_id, None)
    
    def unsubscribe(self, subscription_id: str, pause: bool = False) -> bool:
        """
        Cancel a subscription.
        
        Args:
            subscription_id: The subscription ID to cancel
            pause: If True, pause for later resumption instead of full cancel
        
        Returns:
            True if unsubscribed successfully
        """
        if not self._stub:
            raise ConnectionError("Not connected")
        
        request = sidecar_pb2.UnsubscribeRequest(
            subscription_id=subscription_id,
            pause=pause,
        )
        
        try:
            response = self._stub.Unsubscribe(request)
            
            # Remove from active subscriptions
            with self._subscription_lock:
                self._active_subscriptions.pop(subscription_id, None)
            
            logger.info("Unsubscribed", extra={
                "subscription_id": subscription_id,
                "pause": pause,
            })
            return response.success
        except grpc.RpcError as e:
            logger.error("Unsubscribe failed", extra={
                "subscription_id": subscription_id,
                "error": str(e),
            })
            raise UnsubscribeError(f"Unsubscribe failed: {e.details()}")
    
    def get_topics(self) -> List[TopicInfo]:
        """
        Get list of topics with subscriber counts.
        
        Returns:
            List of TopicInfo objects
        """
        if not self._stub:
            raise ConnectionError("Not connected")
        
        request = sidecar_pb2.GetTopicsRequest()
        
        try:
            response = self._stub.GetTopics(request)
            return [
                TopicInfo(
                    topic=t.topic,
                    subscriber_count=t.subscriber_count,
                    has_retained=t.has_retained,
                )
                for t in response.topics
            ]
        except grpc.RpcError as e:
            raise NexusdError(f"GetTopics failed: {e.details()}")
    
    def get_peers(self) -> List[PeerInfo]:
        """
        Get list of mesh peers.
        
        Returns:
            List of PeerInfo objects
        """
        if not self._stub:
            raise ConnectionError("Not connected")
        
        request = sidecar_pb2.GetPeersRequest()
        
        try:
            response = self._stub.GetPeers(request)
            return [
                PeerInfo(
                    node_id=p.node_id,
                    address=p.address,
                    cluster_id=p.cluster_id,
                    is_healthy=p.is_healthy,
                )
                for p in response.peers
            ]
        except grpc.RpcError as e:
            raise NexusdError(f"GetPeers failed: {e.details()}")


class AsyncNexusdClient:
    """
    Asynchronous client for NexusD pub/sub daemon.
    
    Features:
    - Automatic reconnection with exponential backoff
    - Subscription persistence for recovery
    - Gap detection and message replay
    - Structured logging
    
    Example:
        async with AsyncNexusdClient("localhost:5672") as client:
            await client.publish("topic", b"hello")
            async for msg in client.subscribe(["topic"]):
                print(msg)
    """
    
    def __init__(
        self, 
        address: str = "localhost:5672",
        reconnect_config: Optional[ReconnectConfig] = None,
    ):
        """
        Initialize the async client.
        
        Args:
            address: Daemon address in "host:port" format
            reconnect_config: Reconnection configuration (default: enabled)
        """
        self._address = address
        self._channel: Optional[grpc.aio.Channel] = None
        self._stub: Optional[sidecar_pb2_grpc.SidecarServiceStub] = None
        self._client_id = str(uuid.uuid4())
        self._reconnect_config = reconnect_config or ReconnectConfig()
        
        # Subscription state for recovery
        self._active_subscriptions: Dict[str, SubscriptionInfo] = {}
        
        logger.info("AsyncNexusdClient initialized", extra={
            "address": address,
            "client_id": self._client_id,
            "reconnect_enabled": self._reconnect_config.enabled,
        })
    
    async def connect(self) -> None:
        """Establish connection to the daemon."""
        try:
            self._channel = grpc.aio.insecure_channel(self._address)
            self._stub = sidecar_pb2_grpc.SidecarServiceStub(self._channel)
            logger.info("Connected to daemon", extra={"address": self._address})
        except Exception as e:
            logger.error("Connection failed", extra={
                "address": self._address,
                "error": str(e),
            })
            raise ConnectionError(f"Failed to connect to {self._address}: {e}")
    
    async def _reconnect(self) -> bool:
        """Attempt to reconnect with exponential backoff."""
        import asyncio
        import random
        
        if not self._reconnect_config.enabled:
            return False
        
        delay = self._reconnect_config.initial_delay_ms / 1000.0
        attempt = 0
        
        while True:
            attempt += 1
            if (self._reconnect_config.max_attempts > 0 and 
                attempt > self._reconnect_config.max_attempts):
                logger.error("Max reconnection attempts exceeded", extra={
                    "max_attempts": self._reconnect_config.max_attempts,
                })
                return False
            
            logger.info("Attempting reconnection", extra={
                "attempt": attempt,
                "delay_ms": int(delay * 1000),
            })
            
            await asyncio.sleep(delay)
            
            try:
                await self.connect()
                logger.info("Reconnection successful", extra={"attempt": attempt})
                return True
            except Exception as e:
                logger.warning("Reconnection attempt failed", extra={
                    "attempt": attempt,
                    "error": str(e),
                })
            
            delay = min(
                delay * self._reconnect_config.multiplier,
                self._reconnect_config.max_delay_ms / 1000.0
            )
            jitter = delay * self._reconnect_config.jitter * (2 * random.random() - 1)
            delay += jitter
    
    async def close(self) -> None:
        """Close the connection."""
        if self._channel:
            # Pause subscriptions for potential recovery
            for sub_id in list(self._active_subscriptions.keys()):
                try:
                    await self._pause_subscription(sub_id)
                except Exception:
                    pass
            
            await self._channel.close()
            self._channel = None
            self._stub = None
            logger.info("Connection closed", extra={"address": self._address})
    
    async def __aenter__(self) -> "AsyncNexusdClient":
        await self.connect()
        return self
    
    async def __aexit__(self, exc_type, exc_val, exc_tb) -> None:
        await self.close()
    
    async def _pause_subscription(self, subscription_id: str) -> bool:
        """Pause a subscription for later recovery."""
        if not self._stub:
            return False
        
        try:
            request = sidecar_pb2.UnsubscribeRequest(
                subscription_id=subscription_id,
                pause=True,
            )
            await self._stub.Unsubscribe(request)
            return True
        except Exception:
            return False
    
    async def publish(
        self,
        topic: str,
        payload: bytes,
        content_type: str = "",
        retain: bool = False,
        correlation_id: str = "",
        ttl_ms: int = 0,
    ) -> PublishResult:
        """
        Publish a message to a topic.
        
        Args:
            topic: Target topic
            payload: Message payload as bytes
            content_type: Optional content type hint
            retain: If True, message is retained for late subscribers
            correlation_id: Optional correlation ID for request-response
            ttl_ms: Time-to-live in milliseconds (0 = no expiration)
        
        Returns:
            PublishResult with success status and metadata
        """
        if not self._stub:
            raise ConnectionError("Not connected")
        
        request = sidecar_pb2.PublishRequest(
            topic=topic,
            payload=payload,
            content_type=content_type,
            retain=retain,
            correlation_id=correlation_id,
            ttl_ms=ttl_ms,
        )
        
        try:
            response = await self._stub.Publish(request)
            logger.debug("Message published", extra={
                "topic": topic,
                "message_id": response.message_id,
            })
            return PublishResult(
                success=response.success,
                message_id=response.message_id,
                subscriber_count=response.subscriber_count,
                error_message=response.error_message,
            )
        except grpc.RpcError as e:
            if e.code() == grpc.StatusCode.UNAVAILABLE and await self._reconnect():
                return await self.publish(topic, payload, content_type, retain,
                                         correlation_id, ttl_ms)
            logger.error("Publish failed", extra={"topic": topic, "error": str(e)})
            raise PublishError(f"Publish failed: {e.details()}")
    
    async def subscribe(
        self,
        topics: List[str],
        receive_retained: bool = True,
        max_buffer_size: int = 0,
        gap_recovery_mode: GapRecoveryMode = GapRecoveryMode.REPLAY_BUFFER,
        on_gap_detected: Optional[Callable[[SubscriptionInfo], None]] = None,
    ) -> AsyncIterator[Message]:
        """
        Subscribe to topics and yield messages asynchronously.
        
        Features automatic reconnection and gap recovery.
        
        Args:
            topics: List of topic names to subscribe to
            receive_retained: If True, receive retained messages
            max_buffer_size: Max messages to buffer (0 = unlimited)
            gap_recovery_mode: How to handle gaps after reconnection
            on_gap_detected: Callback when gap is detected
        
        Yields:
            Message objects as they arrive
        """
        if not self._stub:
            raise ConnectionError("Not connected")
        
        subscription_id: Optional[str] = None
        last_sequence: int = 0
        
        while True:
            try:
                if subscription_id and self._reconnect_config.enabled:
                    # Resume with gap recovery
                    request = sidecar_pb2.ResumeSubscribeRequest(
                        subscription_id=subscription_id,
                        last_sequence_number=last_sequence,
                        gap_recovery_mode=gap_recovery_mode.value,
                    )
                    stream = self._stub.ResumeSubscribe(request)
                else:
                    request = sidecar_pb2.SubscribeRequest(
                        topics=topics,
                        client_id=self._client_id,
                        receive_retained=receive_retained,
                        max_buffer_size=max_buffer_size,
                    )
                    stream = self._stub.Subscribe(request)
                
                async for event in stream:
                    event_type = event.WhichOneof("event")
                    
                    if event_type == "subscription_info":
                        info = event.subscription_info
                        subscription_id = info.subscription_id
                        
                        sub_info = SubscriptionInfo(
                            subscription_id=info.subscription_id,
                            topics=list(info.topics),
                            gap_detected=info.gap_detected,
                            missed_message_count=info.missed_message_count,
                            replay_started=info.replay_started,
                            last_sequence=last_sequence,
                        )
                        self._active_subscriptions[subscription_id] = sub_info
                        
                        logger.info("Subscription active", extra={
                            "subscription_id": subscription_id,
                            "gap_detected": info.gap_detected,
                        })
                        
                        if info.gap_detected and on_gap_detected:
                            on_gap_detected(sub_info)
                    
                    elif event_type == "message":
                        msg = event.message
                        last_sequence = msg.sequence_number or last_sequence
                        yield Message(
                            message_id=msg.message_id,
                            topic=msg.topic,
                            payload=msg.payload,
                            content_type=msg.content_type,
                            timestamp_ms=msg.timestamp_ms,
                            source_node_id=msg.source_node_id,
                            correlation_id=msg.correlation_id,
                            is_retained=False,
                            sequence_number=msg.sequence_number,
                        )
                    
                    elif event_type == "retained_message":
                        msg = event.retained_message
                        yield Message(
                            message_id=msg.message_id,
                            topic=msg.topic,
                            payload=msg.payload,
                            content_type=msg.content_type,
                            timestamp_ms=msg.timestamp_ms,
                            source_node_id=msg.source_node_id,
                            correlation_id=msg.correlation_id,
                            is_retained=True,
                        )
                    
                    elif event_type == "replay_message":
                        msg = event.replay_message
                        last_sequence = max(last_sequence, msg.sequence_number)
                        yield Message(
                            message_id=msg.message_id,
                            topic=msg.topic,
                            payload=msg.payload,
                            content_type="",
                            timestamp_ms=msg.timestamp_ms,
                            source_node_id=msg.source_node_id,
                            correlation_id="",
                            is_retained=False,
                            is_replay=True,
                            sequence_number=msg.sequence_number,
                        )
                    
                    elif event_type == "replay_complete":
                        logger.info("Replay complete", extra={
                            "subscription_id": subscription_id,
                        })
                    
                    if subscription_id and subscription_id in self._active_subscriptions:
                        self._active_subscriptions[subscription_id].last_sequence = last_sequence
                
                break
                    
            except grpc.RpcError as e:
                if e.code() == grpc.StatusCode.CANCELLED:
                    break
                
                if e.code() == grpc.StatusCode.UNAVAILABLE:
                    logger.warning("Connection lost during subscription", extra={
                        "subscription_id": subscription_id,
                    })
                    if await self._reconnect():
                        continue
                
                logger.error("Subscribe failed", extra={"error": str(e)})
                raise SubscribeError(f"Subscribe failed: {e.details()}")
        
        if subscription_id:
            self._active_subscriptions.pop(subscription_id, None)
    
    async def unsubscribe(self, subscription_id: str, pause: bool = False) -> bool:
        """Cancel a subscription."""
        if not self._stub:
            raise ConnectionError("Not connected")
        
        request = sidecar_pb2.UnsubscribeRequest(
            subscription_id=subscription_id,
            pause=pause,
        )
        
        try:
            response = await self._stub.Unsubscribe(request)
            self._active_subscriptions.pop(subscription_id, None)
            logger.info("Unsubscribed", extra={
                "subscription_id": subscription_id,
                "pause": pause,
            })
            return response.success
        except grpc.RpcError as e:
            logger.error("Unsubscribe failed", extra={"error": str(e)})
            raise UnsubscribeError(f"Unsubscribe failed: {e.details()}")
    
    async def get_topics(self) -> List[TopicInfo]:
        """Get list of topics with subscriber counts."""
        if not self._stub:
            raise ConnectionError("Not connected")
        
        request = sidecar_pb2.GetTopicsRequest()
        
        try:
            response = await self._stub.GetTopics(request)
            return [
                TopicInfo(
                    topic=t.topic,
                    subscriber_count=t.subscriber_count,
                    has_retained=t.has_retained,
                )
                for t in response.topics
            ]
        except grpc.RpcError as e:
            raise NexusdError(f"GetTopics failed: {e.details()}")
    
    async def get_peers(self) -> List[PeerInfo]:
        """Get list of mesh peers."""
        if not self._stub:
            raise ConnectionError("Not connected")
        
        request = sidecar_pb2.GetPeersRequest()
        
        try:
            response = await self._stub.GetPeers(request)
            return [
                PeerInfo(
                    node_id=p.node_id,
                    address=p.address,
                    cluster_id=p.cluster_id,
                    is_healthy=p.is_healthy,
                )
                for p in response.peers
            ]
        except grpc.RpcError as e:
            raise NexusdError(f"GetPeers failed: {e.details()}")
