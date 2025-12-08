"""
NexusD Client Exceptions
"""


class NexusdError(Exception):
    """Base exception for NexusD client errors."""
    pass


class ConnectionError(NexusdError):
    """Raised when connection to daemon fails."""
    pass


class PublishError(NexusdError):
    """Raised when publish operation fails."""
    pass


class SubscribeError(NexusdError):
    """Raised when subscribe operation fails."""
    pass


class UnsubscribeError(NexusdError):
    """Raised when unsubscribe operation fails."""
    pass
