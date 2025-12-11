/**
 * Example: Subscribing to messages with NexusD Node.js client
 * 
 * Demonstrates auto-reconnection with exponential backoff and gap detection.
 */

import { NexusdClient, GapRecoveryMode, ReconnectionState } from '../src';

async function main() {
    // Connect to daemon with resilience settings
    const client = new NexusdClient('localhost:5672', {
        reconnection: {
            maxRetries: 0,  // 0 = infinite retries
            initialDelayMs: 1000,
            maxDelayMs: 30000,
            exponentialBackoff: true,
            jitterFactor: 0.1,
        },
        gapRecoveryMode: GapRecoveryMode.ReplayBuffer,
        debug: true,  // Enable structured logging
    });

    console.log('Connected to NexusD');
    console.log('Subscribing to sensors/* topics...');
    console.log('Auto-reconnect enabled with gap recovery');
    console.log('Press Ctrl+C to stop\n');

    // Register connection state callback
    client.onConnectionStateChange((state, error) => {
        switch (state) {
            case ReconnectionState.Connected:
                console.log('[Connection] Connected to daemon');
                break;
            case ReconnectionState.Reconnecting:
                console.log(`[Connection] Reconnecting... (${error?.message})`);
                break;
            case ReconnectionState.Disconnected:
                console.log('[Connection] Disconnected');
                break;
            case ReconnectionState.Failed:
                console.log(`[Connection] Failed: ${error?.message}`);
                break;
        }
    });

    // Register gap detection callback
    client.onGapDetected((expected, actual, topic) => {
        const gap = actual - expected;
        console.log(`[Gap] Detected ${gap} missed messages on ${topic}`);
        console.log(`       Expected seq ${expected}, got ${actual}`);
    });

    // Handle shutdown
    process.on('SIGINT', () => {
        console.log('\nShutting down...');
        client.close();
        process.exit(0);
    });

    try {
        // Subscribe to multiple topics with gap recovery
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

            console.log(`  Sequence: ${message.sequenceNumber}`);
            console.log(`  Message ID: ${message.messageId}`);
            console.log(`  Timestamp: ${message.timestampMs}`);
            console.log();
        }
    } finally {
        client.close();
    }
}

main().catch(console.error);
