/**
 * NexusD Node.js/TypeScript Client Implementation
 * 
 * Features:
 * - Automatic reconnection with exponential backoff
 * - Gap detection using sequence numbers
 * - Resume subscriptions after disconnect
 * - Structured logging
 * 
 * Uses dynamic proto loading via @grpc/proto-loader for simplicity.
 */

import * as grpc from '@grpc/grpc-js';
import * as protoLoader from '@grpc/proto-loader';
import * as path from 'path';
import { v4 as uuidv4 } from 'uuid';
import {
    Message,
    MessageImpl,
    PublishResult,
    PublishOptions,
    SubscribeOptions,
    TopicInfo,
    PeerInfo,
    ClientOptions,
    ReconnectionPolicy,
    ReconnectionState,
    GapRecoveryMode,
    SubscriptionInfo,
    ConnectionStateCallback,
    GapDetectedCallback,
    DEFAULT_RECONNECTION_POLICY,
} from './types';

// Load proto definition
const PROTO_PATH = path.resolve(__dirname, '../../..', 'proto', 'sidecar.proto');

const packageDefinition = protoLoader.loadSync(PROTO_PATH, {
    keepCase: false,
    longs: Number,
    enums: String,
    defaults: true,
    oneofs: true,
});

const protoDescriptor = grpc.loadPackageDefinition(packageDefinition) as any;
const SidecarService = protoDescriptor.nexusd.sidecar.SidecarService;

/** Logger interface */
interface Logger {
    debug(msg: string, ...args: any[]): void;
    info(msg: string, ...args: any[]): void;
    warn(msg: string, ...args: any[]): void;
    error(msg: string, ...args: any[]): void;
}

/** Default console logger */
const defaultLogger: Logger = {
    debug: (msg, ...args) => console.debug(`[NexusD DEBUG] ${msg}`, ...args),
    info: (msg, ...args) => console.info(`[NexusD INFO] ${msg}`, ...args),
    warn: (msg, ...args) => console.warn(`[NexusD WARN] ${msg}`, ...args),
    error: (msg, ...args) => console.error(`[NexusD ERROR] ${msg}`, ...args),
};

/** Silent logger (for non-debug mode) */
const silentLogger: Logger = {
    debug: () => {},
    info: () => {},
    warn: () => {},
    error: (msg, ...args) => console.error(`[NexusD ERROR] ${msg}`, ...args),
};

/**
 * NexusD Client for Node.js with automatic reconnection support.
 * 
 * @example
 * ```typescript
 * const client = new NexusdClient('localhost:5672', {
 *     reconnection: { maxRetries: 10 },
 *     gapRecoveryMode: GapRecoveryMode.ReplayBuffer,
 *     debug: true,
 * });
 * 
 * client.onConnectionStateChange((state, error) => {
 *     console.log('Connection state:', state, error?.message);
 * });
 * 
 * await client.publish('topic', Buffer.from('hello'));
 * 
 * for await (const msg of client.subscribe(['topic'])) {
 *     console.log(msg.topic, msg.payloadAsString());
 * }
 * 
 * client.close();
 * ```
 */
export class NexusdClient {
    private client: any;
    private clientId: string;
    private address: string;
    private options: ClientOptions;
    private reconnectionPolicy: ReconnectionPolicy;
    private connectionState: ReconnectionState = ReconnectionState.Connected;
    private activeSubscriptions: Map<string, SubscriptionInfo> = new Map();
    private connectionStateCallbacks: ConnectionStateCallback[] = [];
    private gapDetectedCallbacks: GapDetectedCallback[] = [];
    private logger: Logger;
    private closed: boolean = false;

    /**
     * Create a new NexusD client.
     * @param address Daemon address in "host:port" format
     * @param options Client options including reconnection policy
     */
    constructor(address: string = 'localhost:5672', options: ClientOptions = {}) {
        this.address = address;
        this.options = options;
        this.logger = options.debug ? defaultLogger : silentLogger;
        
        this.reconnectionPolicy = {
            ...DEFAULT_RECONNECTION_POLICY,
            ...options.reconnection,
        };
        
        this.client = new SidecarService(
            address,
            grpc.credentials.createInsecure()
        );
        this.clientId = uuidv4();
        
        this.logger.info('Client created', { address, clientId: this.clientId });
    }

    /**
     * Register a callback for connection state changes.
     */
    onConnectionStateChange(callback: ConnectionStateCallback): void {
        this.connectionStateCallbacks.push(callback);
    }

    /**
     * Register a callback for gap detection events.
     */
    onGapDetected(callback: GapDetectedCallback): void {
        this.gapDetectedCallbacks.push(callback);
    }

    /**
     * Get the current connection state.
     */
    getConnectionState(): ReconnectionState {
        return this.connectionState;
    }

    /**
     * Get list of active subscriptions.
     */
    getActiveSubscriptions(): SubscriptionInfo[] {
        return Array.from(this.activeSubscriptions.values());
    }

    private setConnectionState(state: ReconnectionState, error?: Error): void {
        const previousState = this.connectionState;
        this.connectionState = state;
        
        if (previousState !== state) {
            this.logger.info('Connection state changed', { from: previousState, to: state });
            for (const callback of this.connectionStateCallbacks) {
                try {
                    callback(state, error);
                } catch (e) {
                    this.logger.error('Error in connection state callback', e);
                }
            }
        }
    }

    private calculateDelay(attempt: number): number {
        let delay = this.reconnectionPolicy.initialDelayMs;
        
        if (this.reconnectionPolicy.exponentialBackoff) {
            delay = Math.min(
                this.reconnectionPolicy.initialDelayMs * Math.pow(2, attempt),
                this.reconnectionPolicy.maxDelayMs
            );
        }
        
        // Add jitter
        const jitter = delay * this.reconnectionPolicy.jitterFactor * Math.random();
        return Math.floor(delay + jitter);
    }

    private sleep(ms: number): Promise<void> {
        return new Promise(resolve => setTimeout(resolve, ms));
    }

    private async reconnect(): Promise<void> {
        grpc.closeClient(this.client);
        this.client = new SidecarService(
            this.address,
            grpc.credentials.createInsecure()
        );
        this.logger.info('Reconnected to server');
    }

    private async withRetry<T>(
        operation: () => Promise<T>,
        operationName: string
    ): Promise<T> {
        let attempt = 0;
        const maxRetries = this.reconnectionPolicy.maxRetries;
        
        while (!this.closed) {
            try {
                return await operation();
            } catch (error: any) {
                const isConnectionError = 
                    error?.code === grpc.status.UNAVAILABLE ||
                    error?.code === grpc.status.DEADLINE_EXCEEDED;
                
                if (!isConnectionError) {
                    throw error;
                }
                
                attempt++;
                this.setConnectionState(ReconnectionState.Reconnecting, error);
                
                if (maxRetries > 0 && attempt >= maxRetries) {
                    this.setConnectionState(ReconnectionState.Failed, error);
                    throw error;
                }
                
                const delay = this.calculateDelay(attempt);
                this.logger.warn(`${operationName} failed, retrying in ${delay}ms`, {
                    attempt,
                    maxRetries: maxRetries || 'infinite',
                    error: error.message,
                });
                
                await this.sleep(delay);
                await this.reconnect();
            }
        }
        
        throw new Error('Client closed');
    }

    /**
     * Publish a message to a topic with automatic retry on connection failure.
     */
    async publish(
        topic: string,
        payload: Buffer,
        options: PublishOptions = {}
    ): Promise<PublishResult> {
        return this.withRetry(async () => {
            return new Promise((resolve, reject) => {
                const request = {
                    topic,
                    payload,
                    contentType: options.contentType || '',
                    retain: options.retain || false,
                    correlationId: options.correlationId || '',
                    ttlMs: options.ttlMs || 0,
                };

                this.client.publish(request, (error: any, response: any) => {
                    if (error) {
                        reject(error);
                    } else {
                        this.setConnectionState(ReconnectionState.Connected);
                        this.logger.debug('Published message', { topic, messageId: response.messageId });
                        resolve({
                            success: response.success,
                            messageId: response.messageId,
                            subscriberCount: response.subscriberCount,
                            errorMessage: response.errorMessage,
                        });
                    }
                });
            });
        }, 'publish');
    }

    /**
     * Subscribe to topics with automatic reconnection and gap detection.
     * Yields messages as an async iterable.
     */
    async *subscribe(
        topics: string[],
        options: SubscribeOptions = {}
    ): AsyncIterable<Message> {
        const subscriptionId = options.subscriptionId || uuidv4();
        const gapRecoveryMode = options.gapRecoveryMode ?? this.options.gapRecoveryMode ?? GapRecoveryMode.None;
        let lastSequenceNumber = 0;
        let isResuming = false;
        
        const subscriptionInfo: SubscriptionInfo = {
            subscriptionId,
            topics,
            lastSequenceNumber: 0,
            gapRecoveryMode,
            isActive: true,
        };
        this.activeSubscriptions.set(subscriptionId, subscriptionInfo);
        
        this.logger.info('Starting subscription', { subscriptionId, topics, gapRecoveryMode });

        while (!this.closed && subscriptionInfo.isActive) {
            try {
                let request: any;
                
                if (isResuming && lastSequenceNumber > 0 && gapRecoveryMode !== GapRecoveryMode.None) {
                    // Resume subscription with gap recovery
                    request = {
                        subscriptionId,
                        topics,
                        clientId: options.clientId || this.clientId,
                        lastSequenceNumber,
                        gapRecoveryMode,
                    };
                    this.logger.info('Resuming subscription', { subscriptionId, lastSequenceNumber });
                } else {
                    // Regular subscribe
                    request = {
                        topics,
                        clientId: options.clientId || this.clientId,
                        receiveRetained: options.receiveRetained ?? true,
                        maxBufferSize: options.maxBufferSize || 0,
                        subscriptionId: subscriptionId,
                    };
                }

                const call = isResuming && lastSequenceNumber > 0 && gapRecoveryMode !== GapRecoveryMode.None
                    ? this.client.resumeSubscribe(request)
                    : this.client.subscribe(request);

                // Process stream events
                const messageQueue: Message[] = [];
                let resolver: ((value: IteratorResult<Message>) => void) | null = null;
                let done = false;
                let streamError: Error | null = null;

                call.on('data', (event: any) => {
                    let message: Message | null = null;
                    let seqNum = 0;

                    if (event.message) {
                        const m = event.message;
                        seqNum = m.sequenceNumber || 0;
                        message = new MessageImpl(
                            m.messageId,
                            m.topic,
                            Buffer.from(m.payload),
                            m.contentType,
                            m.timestampMs,
                            m.sourceNodeId,
                            m.correlationId,
                            false,
                            seqNum
                        );
                    } else if (event.retainedMessage) {
                        const m = event.retainedMessage;
                        seqNum = m.sequenceNumber || 0;
                        message = new MessageImpl(
                            m.messageId,
                            m.topic,
                            Buffer.from(m.payload),
                            m.contentType,
                            m.timestampMs,
                            m.sourceNodeId,
                            m.correlationId,
                            true,
                            seqNum
                        );
                    } else if (event.replayMessage) {
                        const m = event.replayMessage;
                        seqNum = m.sequenceNumber || 0;
                        this.logger.debug('Received replay message', { seqNum, topic: m.topic });
                        message = new MessageImpl(
                            m.messageId,
                            m.topic,
                            Buffer.from(m.payload),
                            m.contentType,
                            m.timestampMs,
                            m.sourceNodeId,
                            m.correlationId,
                            false,
                            seqNum
                        );
                    } else if (event.replayComplete) {
                        const rc = event.replayComplete;
                        this.logger.info('Replay complete', { replayedCount: rc.replayedCount });
                        return;
                    }

                    if (message && seqNum > 0) {
                        // Gap detection
                        if (lastSequenceNumber > 0 && seqNum > lastSequenceNumber + 1) {
                            const gap = seqNum - lastSequenceNumber - 1;
                            this.logger.warn('Gap detected', { 
                                expected: lastSequenceNumber + 1, 
                                actual: seqNum, 
                                gap,
                                topic: message.topic 
                            });
                            for (const callback of this.gapDetectedCallbacks) {
                                try {
                                    callback(lastSequenceNumber + 1, seqNum, message.topic);
                                } catch (e) {
                                    this.logger.error('Error in gap detection callback', e);
                                }
                            }
                        }
                        lastSequenceNumber = seqNum;
                        subscriptionInfo.lastSequenceNumber = seqNum;
                    }

                    if (message) {
                        this.setConnectionState(ReconnectionState.Connected);
                        if (resolver) {
                            resolver({ value: message, done: false });
                            resolver = null;
                        } else {
                            messageQueue.push(message);
                        }
                    }
                });

                call.on('end', () => {
                    done = true;
                    this.logger.debug('Stream ended');
                    if (resolver) {
                        resolver({ value: undefined as any, done: true });
                        resolver = null;
                    }
                });

                call.on('error', (err: Error) => {
                    streamError = err;
                    done = true;
                    this.logger.error('Stream error', { error: err.message, code: (err as any).code });
                    if (resolver) {
                        resolver({ value: undefined as any, done: true });
                        resolver = null;
                    }
                });

                // Yield messages from this stream
                while (!done || messageQueue.length > 0) {
                    if (messageQueue.length > 0) {
                        yield messageQueue.shift()!;
                    } else if (!done) {
                        const result = await new Promise<IteratorResult<Message>>((resolve) => {
                            resolver = resolve;
                        });
                        if (!result.done) {
                            yield result.value;
                        }
                    }
                }

                // Check if we need to reconnect
                if (streamError) {
                    const code = (streamError as any).code;
                    if (code === grpc.status.CANCELLED) {
                        // Intentional cancellation, stop
                        break;
                    }
                    if (code === grpc.status.UNAVAILABLE || code === grpc.status.DEADLINE_EXCEEDED) {
                        // Connection lost, try to reconnect
                        isResuming = true;
                        this.setConnectionState(ReconnectionState.Reconnecting, streamError);
                        
                        const delay = this.calculateDelay(1);
                        this.logger.warn(`Subscription interrupted, reconnecting in ${delay}ms`, {
                            subscriptionId,
                            lastSequenceNumber,
                        });
                        await this.sleep(delay);
                        await this.reconnect();
                        continue;
                    }
                    // Other errors, stop
                    throw streamError;
                }
                
                // Stream ended normally without error
                break;
                
            } catch (error: any) {
                if (this.closed || !subscriptionInfo.isActive) {
                    break;
                }
                
                this.setConnectionState(ReconnectionState.Reconnecting, error);
                isResuming = true;
                
                const delay = this.calculateDelay(1);
                this.logger.warn(`Subscription error, retrying in ${delay}ms`, {
                    subscriptionId,
                    error: error.message,
                });
                await this.sleep(delay);
                await this.reconnect();
            }
        }
        
        this.activeSubscriptions.delete(subscriptionId);
        this.logger.info('Subscription ended', { subscriptionId });
    }

    /**
     * Unsubscribe from a subscription.
     * @param subscriptionId The subscription ID to unsubscribe
     * @param pause If true, pause the subscription for later resumption instead of terminating
     */
    async unsubscribe(subscriptionId: string, pause: boolean = false): Promise<boolean> {
        // Mark subscription as inactive
        const subInfo = this.activeSubscriptions.get(subscriptionId);
        if (subInfo) {
            subInfo.isActive = false;
            if (!pause) {
                this.activeSubscriptions.delete(subscriptionId);
            }
        }
        
        return new Promise((resolve, reject) => {
            const request = { subscriptionId, pause };

            this.client.unsubscribe(request, (error: any, response: any) => {
                if (error) {
                    reject(error);
                } else {
                    this.logger.info('Unsubscribed', { subscriptionId, pause, success: response.success });
                    resolve(response.success);
                }
            });
        });
    }

    /**
     * Get list of topics with subscriber counts.
     */
    async getTopics(): Promise<TopicInfo[]> {
        return this.withRetry(async () => {
            return new Promise((resolve, reject) => {
                this.client.getTopics({}, (error: any, response: any) => {
                    if (error) {
                        reject(error);
                    } else {
                        this.setConnectionState(ReconnectionState.Connected);
                        resolve(
                            response.topics.map((t: any) => ({
                                topic: t.topic,
                                subscriberCount: t.subscriberCount,
                                hasRetained: t.hasRetained,
                            }))
                        );
                    }
                });
            });
        }, 'getTopics');
    }

    /**
     * Get list of mesh peers.
     */
    async getPeers(): Promise<PeerInfo[]> {
        return this.withRetry(async () => {
            return new Promise((resolve, reject) => {
                this.client.getPeers({}, (error: any, response: any) => {
                    if (error) {
                        reject(error);
                    } else {
                        this.setConnectionState(ReconnectionState.Connected);
                        resolve(
                            response.peers.map((p: any) => ({
                                nodeId: p.nodeId,
                                address: p.address,
                                clusterId: p.clusterId,
                                isHealthy: p.isHealthy,
                            }))
                        );
                    }
                });
            });
        }, 'getPeers');
    }

    /**
     * Close the client connection and stop all subscriptions.
     */
    close(): void {
        this.closed = true;
        
        // Mark all subscriptions as inactive
        for (const [id, subInfo] of this.activeSubscriptions) {
            subInfo.isActive = false;
        }
        this.activeSubscriptions.clear();
        
        grpc.closeClient(this.client);
        this.setConnectionState(ReconnectionState.Disconnected);
        this.logger.info('Client closed');
    }
}
