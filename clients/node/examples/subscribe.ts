/**
 * Example: Subscribing to messages with NexusD Node.js client
 */

import { NexusdClient } from '../src';

async function main() {
    // Connect to daemon
    const client = new NexusdClient('localhost:5672');
    console.log('Connected to NexusD');
    console.log('Subscribing to sensors/* topics...');
    console.log('Press Ctrl+C to stop\n');

    // Handle shutdown
    process.on('SIGINT', () => {
        console.log('\nShutting down...');
        client.close();
        process.exit(0);
    });

    try {
        // Subscribe to multiple topics
        const topics = ['sensors/temperature', 'sensors/humidity', 'config/settings'];

        for await (const message of client.subscribe(topics, { receiveRetained: true })) {
            const retainedFlag = message.isRetained ? ' [RETAINED]' : '';
            console.log(`Topic: ${message.topic}${retainedFlag}`);

            try {
                if (message.contentType === 'application/json') {
                    console.log(`  Payload: ${JSON.stringify(message.payloadAsJson())}`);
                } else {
                    console.log(`  Payload: ${message.payloadAsString()}`);
                }
            } catch {
                console.log(`  Payload: ${message.payload.toString('hex')}`);
            }

            console.log(`  Message ID: ${message.messageId}`);
            console.log(`  Timestamp: ${message.timestampMs}`);
            console.log();
        }
    } finally {
        client.close();
    }
}

main().catch(console.error);
