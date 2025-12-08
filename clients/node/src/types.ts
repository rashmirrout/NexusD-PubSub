/**
 * NexusD Client Types
 */

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
}

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
        public isRetained: boolean
    ) {}
    
    payloadAsString(): string {
        return this.payload.toString('utf-8');
    }
    
    payloadAsJson<T = unknown>(): T {
        return JSON.parse(this.payloadAsString());
    }
}
