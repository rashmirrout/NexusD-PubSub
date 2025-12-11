/**
 * NexusD Node.js/TypeScript Client
 * 
 * Provides pub/sub messaging with automatic reconnection,
 * gap detection, and structured logging.
 */

export { NexusdClient } from './client';
export {
    Message,
    PublishResult,
    PublishOptions,
    SubscribeOptions,
    TopicInfo,
    PeerInfo,
    // Reconnection types
    GapRecoveryMode,
    ReconnectionPolicy,
    ReconnectionState,
    ClientOptions,
    SubscriptionInfo,
    ConnectionStateCallback,
    GapDetectedCallback,
    DEFAULT_RECONNECTION_POLICY,
} from './types';
