/**
 * NexusD Node.js/TypeScript Client Implementation
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

/**
 * NexusD Client for Node.js
 * 
 * @example
 * ```typescript
 * const client = new NexusdClient('localhost:5672');
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

    /**
     * Create a new NexusD client.
     * @param address Daemon address in "host:port" format
     */
    constructor(address: string = 'localhost:5672') {
        this.client = new SidecarService(
            address,
            grpc.credentials.createInsecure()
        );
        this.clientId = uuidv4();
    }

    /**
     * Publish a message to a topic.
     */
    async publish(
        topic: string,
        payload: Buffer,
        options: PublishOptions = {}
    ): Promise<PublishResult> {
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
                    resolve({
                        success: response.success,
                        messageId: response.messageId,
                        subscriberCount: response.subscriberCount,
                        errorMessage: response.errorMessage,
                    });
                }
            });
        });
    }

    /**
     * Subscribe to topics and receive messages as an async iterable.
     */
    async *subscribe(
        topics: string[],
        options: SubscribeOptions = {}
    ): AsyncIterable<Message> {
        const request = {
            topics,
            clientId: options.clientId || this.clientId,
            receiveRetained: options.receiveRetained ?? true,
            maxBufferSize: options.maxBufferSize || 0,
        };

        const call = this.client.subscribe(request);

        // Create an async iterator from the stream
        const messageQueue: Message[] = [];
        let resolver: ((value: IteratorResult<Message>) => void) | null = null;
        let done = false;
        let error: Error | null = null;

        call.on('data', (event: any) => {
            let message: Message | null = null;

            if (event.message) {
                const m = event.message;
                message = new MessageImpl(
                    m.messageId,
                    m.topic,
                    Buffer.from(m.payload),
                    m.contentType,
                    m.timestampMs,
                    m.sourceNodeId,
                    m.correlationId,
                    false
                );
            } else if (event.retainedMessage) {
                const m = event.retainedMessage;
                message = new MessageImpl(
                    m.messageId,
                    m.topic,
                    Buffer.from(m.payload),
                    m.contentType,
                    m.timestampMs,
                    m.sourceNodeId,
                    m.correlationId,
                    true
                );
            }

            if (message) {
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
            if (resolver) {
                resolver({ value: undefined as any, done: true });
                resolver = null;
            }
        });

        call.on('error', (err: Error) => {
            error = err;
            done = true;
            if (resolver) {
                resolver({ value: undefined as any, done: true });
                resolver = null;
            }
        });

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

        if (error && (error as any).code !== grpc.status.CANCELLED) {
            throw error;
        }
    }

    /**
     * Unsubscribe from a subscription.
     */
    async unsubscribe(subscriptionId: string): Promise<boolean> {
        return new Promise((resolve, reject) => {
            const request = { subscriptionId };

            this.client.unsubscribe(request, (error: any, response: any) => {
                if (error) {
                    reject(error);
                } else {
                    resolve(response.success);
                }
            });
        });
    }

    /**
     * Get list of topics with subscriber counts.
     */
    async getTopics(): Promise<TopicInfo[]> {
        return new Promise((resolve, reject) => {
            this.client.getTopics({}, (error: any, response: any) => {
                if (error) {
                    reject(error);
                } else {
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
    }

    /**
     * Get list of mesh peers.
     */
    async getPeers(): Promise<PeerInfo[]> {
        return new Promise((resolve, reject) => {
            this.client.getPeers({}, (error: any, response: any) => {
                if (error) {
                    reject(error);
                } else {
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
    }

    /**
     * Close the client connection.
     */
    close(): void {
        grpc.closeClient(this.client);
    }
}
