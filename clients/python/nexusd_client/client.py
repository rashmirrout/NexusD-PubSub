"""
NexusD Client Implementation

Provides synchronous and asynchronous clients for the NexusD pub/sub daemon.
"""

from __future__ import annotations

import uuid
from typing import Iterator, AsyncIterator, Optional, List
from dataclasses import dataclass

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


class NexusdClient:
    """
    Synchronous client for NexusD pub/sub daemon.
    
    Example:
        client = NexusdClient("localhost:5672")
        client.publish("topic", b"hello")
        for msg in client.subscribe(["topic"]):
            print(msg)
        client.close()
    """
    
    def __init__(self, address: str = "localhost:5672"):
        """
        Initialize the client.
        
        Args:
            address: Daemon address in "host:port" format
        """
        self._address = address
        self._channel: Optional[grpc.Channel] = None
        self._stub: Optional[sidecar_pb2_grpc.SidecarServiceStub] = None
        self._client_id = str(uuid.uuid4())
        self._connect()
    
    def _connect(self) -> None:
        """Establish connection to the daemon."""
        try:
            self._channel = grpc.insecure_channel(self._address)
            self._stub = sidecar_pb2_grpc.SidecarServiceStub(self._channel)
        except Exception as e:
            raise ConnectionError(f"Failed to connect to {self._address}: {e}")
    
    def close(self) -> None:
        """Close the connection."""
        if self._channel:
            self._channel.close()
            self._channel = None
            self._stub = None
    
    def __enter__(self) -> "NexusdClient":
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        self.close()
    
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
            PublishError: If publish fails
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
    ) -> Iterator[Message]:
        """
        Subscribe to topics and yield messages.
        
        Args:
            topics: List of topic names to subscribe to
            receive_retained: If True, receive retained messages
            max_buffer_size: Max messages to buffer (0 = unlimited)
        
        Yields:
            Message objects as they arrive
        
        Raises:
            SubscribeError: If subscription fails
        """
        if not self._stub:
            raise ConnectionError("Not connected")
        
        request = sidecar_pb2.SubscribeRequest(
            topics=topics,
            client_id=self._client_id,
            receive_retained=receive_retained,
            max_buffer_size=max_buffer_size,
        )
        
        try:
            for event in self._stub.Subscribe(request):
                # Handle different event types
                event_type = event.WhichOneof("event")
                
                if event_type == "message":
                    msg = event.message
                    yield Message(
                        message_id=msg.message_id,
                        topic=msg.topic,
                        payload=msg.payload,
                        content_type=msg.content_type,
                        timestamp_ms=msg.timestamp_ms,
                        source_node_id=msg.source_node_id,
                        correlation_id=msg.correlation_id,
                        is_retained=False,
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
                # Ignore subscription_info and heartbeat events
                
        except grpc.RpcError as e:
            if e.code() == grpc.StatusCode.CANCELLED:
                return  # Normal cancellation
            raise SubscribeError(f"Subscribe failed: {e.details()}")
    
    def unsubscribe(self, subscription_id: str) -> bool:
        """
        Cancel a subscription.
        
        Args:
            subscription_id: The subscription ID to cancel
        
        Returns:
            True if unsubscribed successfully
        """
        if not self._stub:
            raise ConnectionError("Not connected")
        
        request = sidecar_pb2.UnsubscribeRequest(
            subscription_id=subscription_id,
        )
        
        try:
            response = self._stub.Unsubscribe(request)
            return response.success
        except grpc.RpcError as e:
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
    
    Example:
        async with AsyncNexusdClient("localhost:5672") as client:
            await client.publish("topic", b"hello")
            async for msg in client.subscribe(["topic"]):
                print(msg)
    """
    
    def __init__(self, address: str = "localhost:5672"):
        """
        Initialize the async client.
        
        Args:
            address: Daemon address in "host:port" format
        """
        self._address = address
        self._channel: Optional[grpc.aio.Channel] = None
        self._stub: Optional[sidecar_pb2_grpc.SidecarServiceStub] = None
        self._client_id = str(uuid.uuid4())
    
    async def connect(self) -> None:
        """Establish connection to the daemon."""
        try:
            self._channel = grpc.aio.insecure_channel(self._address)
            self._stub = sidecar_pb2_grpc.SidecarServiceStub(self._channel)
        except Exception as e:
            raise ConnectionError(f"Failed to connect to {self._address}: {e}")
    
    async def close(self) -> None:
        """Close the connection."""
        if self._channel:
            await self._channel.close()
            self._channel = None
            self._stub = None
    
    async def __aenter__(self) -> "AsyncNexusdClient":
        await self.connect()
        return self
    
    async def __aexit__(self, exc_type, exc_val, exc_tb) -> None:
        await self.close()
    
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
            return PublishResult(
                success=response.success,
                message_id=response.message_id,
                subscriber_count=response.subscriber_count,
                error_message=response.error_message,
            )
        except grpc.RpcError as e:
            raise PublishError(f"Publish failed: {e.details()}")
    
    async def subscribe(
        self,
        topics: List[str],
        receive_retained: bool = True,
        max_buffer_size: int = 0,
    ) -> AsyncIterator[Message]:
        """
        Subscribe to topics and yield messages asynchronously.
        
        Args:
            topics: List of topic names to subscribe to
            receive_retained: If True, receive retained messages
            max_buffer_size: Max messages to buffer (0 = unlimited)
        
        Yields:
            Message objects as they arrive
        """
        if not self._stub:
            raise ConnectionError("Not connected")
        
        request = sidecar_pb2.SubscribeRequest(
            topics=topics,
            client_id=self._client_id,
            receive_retained=receive_retained,
            max_buffer_size=max_buffer_size,
        )
        
        try:
            async for event in self._stub.Subscribe(request):
                event_type = event.WhichOneof("event")
                
                if event_type == "message":
                    msg = event.message
                    yield Message(
                        message_id=msg.message_id,
                        topic=msg.topic,
                        payload=msg.payload,
                        content_type=msg.content_type,
                        timestamp_ms=msg.timestamp_ms,
                        source_node_id=msg.source_node_id,
                        correlation_id=msg.correlation_id,
                        is_retained=False,
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
                    
        except grpc.RpcError as e:
            if e.code() == grpc.StatusCode.CANCELLED:
                return
            raise SubscribeError(f"Subscribe failed: {e.details()}")
    
    async def unsubscribe(self, subscription_id: str) -> bool:
        """Cancel a subscription."""
        if not self._stub:
            raise ConnectionError("Not connected")
        
        request = sidecar_pb2.UnsubscribeRequest(
            subscription_id=subscription_id,
        )
        
        try:
            response = await self._stub.Unsubscribe(request)
            return response.success
        except grpc.RpcError as e:
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
