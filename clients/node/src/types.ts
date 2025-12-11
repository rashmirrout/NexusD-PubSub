/**
 * NexusD Client Types
 * 
 * Provides TypeScript types for the NexusD client with auto-reconnection,
 * gap detection, and structured logging support.
 */

/**
 * Gap recovery mode for handling missed messages during reconnection.
 */
export enum GapRecoveryMode {
    /** No gap recovery - just continue receiving new messages */
    None = 0,
    /** Only receive retained messages on reconnect */
    RetainedOnly = 1,
    /** Replay messages from buffer starting from last sequence number */
    ReplayBuffer = 2,
}

/**
 * Reconnection policy configuration.
 */
export interface ReconnectionPolicy {
    /** Maximum number of reconnection attempts (0 = infinite) */
    maxRetries: number;
    /** Initial delay between retries in milliseconds */
    initialDelayMs: number;
    /** Maximum delay between retries in milliseconds */
    maxDelayMs: number;
    /** Whether to use exponential backoff */
    exponentialBackoff: boolean;
    /** Jitter factor (0.0 to 1.0) */
    jitterFactor: number;
}

/**
 * Client options for configuring the NexusD client.
 */
export interface ClientOptions {
    /** Reconnection policy */
    reconnection?: Partial<ReconnectionPolicy>;
    /** Default gap recovery mode */
    gapRecoveryMode?: GapRecoveryMode;
    /** Enable debug logging */
    debug?: boolean;
}

/**
 * Reconnection state.
 */
export enum ReconnectionState {
    Connected = 'connected',
    Disconnected = 'disconnected',
    Reconnecting = 'reconnecting',
    Failed = 'failed',
}

/**
 * Subscription state information.
 */
export interface SubscriptionInfo {
    subscriptionId: string;
    topics: string[];
    lastSequenceNumber: number;
    gapRecoveryMode: GapRecoveryMode;
    isActive: boolean;
}

/**
 * A received message from a subscription.
 */
export interface Message {
    messageId: string;
    topic: string;
    payload: Buffer;
    contentType: string;
    timestampMs: number;
    sourceNodeId: string;
    correlationId: string;
    isRetained: boolean;
    /** Sequence number for gap detection */
    sequenceNumber: number;
    
    /** Get payload as a UTF-8 string */
    payloadAsString(): string;
    
    /** Parse payload as JSON */
    payloadAsJson<T = unknown>(): T;
}

/**
 * Result of a publish operation.
 */
export interface PublishResult {
    success: boolean;
    messageId: string;
    subscriberCount: number;
    errorMessage: string;
}

/**
 * Options for publish operations.
 */
export interface PublishOptions {
    contentType?: string;
    retain?: boolean;
    correlationId?: string;
    ttlMs?: number;
}

/**
 * Options for subscribe operations.
 */
export interface SubscribeOptions {
    clientId?: string;
    receiveRetained?: boolean;
    maxBufferSize?: number;
    /** Gap recovery mode for reconnection */
    gapRecoveryMode?: GapRecoveryMode;
    /** Existing subscription ID for resuming */
    subscriptionId?: string;
}

/**
 * Callback for connection state changes.
 */
export type ConnectionStateCallback = (state: ReconnectionState, error?: Error) => void;

/**
 * Callback for gap detection.
 */
export type GapDetectedCallback = (expectedSeq: number, actualSeq: number, topic: string) => void;

/**
 * Topic information.
 */
export interface TopicInfo {
    topic: string;
    subscriberCount: number;
    hasRetained: boolean;
}

/**
 * Mesh peer information.
 */
export interface PeerInfo {
    nodeId: string;
    address: string;
    clusterId: string;
    isHealthy: boolean;
}

/**
 * Internal message class implementation.
 */
export class MessageImpl implements Message {
    constructor(
        public messageId: string,
        public topic: string,
        public payload: Buffer,
        public contentType: string,
        public timestampMs: number,
        public sourceNodeId: string,
        public correlationId: string,
        public isRetained: boolean,
        public sequenceNumber: number = 0
    ) {}
    
    payloadAsString(): string {
        return this.payload.toString('utf-8');
    }
    
    payloadAsJson<T = unknown>(): T {
        return JSON.parse(this.payloadAsString());
    }
}

/**
 * Default reconnection policy.
 */
export const DEFAULT_RECONNECTION_POLICY: ReconnectionPolicy = {
    maxRetries: 0, // infinite
    initialDelayMs: 1000,
    maxDelayMs: 30000,
    exponentialBackoff: true,
    jitterFactor: 0.1,
};
