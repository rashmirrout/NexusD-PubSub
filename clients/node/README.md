# NexusD Node.js/TypeScript Client

Node.js client library for the NexusD pub/sub sidecar daemon.

## Installation

```bash
npm install @nexusd/client
# or
yarn add @nexusd/client
```

## Building from Source

```bash
cd clients/node
npm install
npm run build
```

## Usage

### Basic Publish/Subscribe

```typescript
import { NexusdClient } from '@nexusd/client';

async function main() {
    // Connect to daemon
    const client = new NexusdClient('localhost:5672');
    
    // Publish a message
    const result = await client.publish(
        'sensors/temperature',
        Buffer.from(JSON.stringify({ value: 23.5 })),
        { contentType: 'application/json' }
    );
    console.log(`Published to ${result.subscriberCount} subscribers`);
    
    // Subscribe to topics
    const subscription = client.subscribe(['sensors/temperature']);
    
    for await (const message of subscription) {
        console.log(`Received: ${message.topic} -> ${message.payloadAsString()}`);
    }
    
    client.close();
}

main();
```

### With Options

```typescript
// Publish with retain
await client.publish(
    'config/settings',
    Buffer.from('{"theme": "dark"}'),
    { contentType: 'application/json', retain: true }
);

// Subscribe with retained messages
const subscription = client.subscribe(['config/#'], {
    receiveRetained: true,
});

for await (const msg of subscription) {
    console.log(msg.isRetained ? '[RETAINED]' : '', msg.topic);
}
```

## API Reference

### NexusdClient

```typescript
class NexusdClient {
    constructor(address?: string);  // Default: 'localhost:5672'
    
    publish(
        topic: string,
        payload: Buffer,
        options?: PublishOptions
    ): Promise<PublishResult>;
    
    subscribe(
        topics: string[],
        options?: SubscribeOptions
    ): AsyncIterable<Message>;
    
    unsubscribe(subscriptionId: string): Promise<boolean>;
    
    getTopics(): Promise<TopicInfo[]>;
    
    getPeers(): Promise<PeerInfo[]>;
    
    close(): void;
}
```

### Types

```typescript
interface Message {
    messageId: string;
    topic: string;
    payload: Buffer;
    contentType: string;
    timestampMs: number;
    sourceNodeId: string;
    correlationId: string;
    isRetained: boolean;
    
    payloadAsString(): string;
    payloadAsJson<T>(): T;
}

interface PublishResult {
    success: boolean;
    messageId: string;
    subscriberCount: number;
    errorMessage: string;
}

interface PublishOptions {
    contentType?: string;
    retain?: boolean;
    correlationId?: string;
    ttlMs?: number;
}

interface SubscribeOptions {
    clientId?: string;
    receiveRetained?: boolean;
    maxBufferSize?: number;
}
```

## Requirements

- Node.js 18+
- NexusD daemon running on target address
