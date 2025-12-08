/**
 * Example: Publishing messages with NexusD Node.js client
 */

import { NexusdClient } from '../src';

async function main() {
    // Connect to daemon
    const client = new NexusdClient('localhost:5672');
    console.log('Connected to NexusD\n');

    try {
        // Publish a simple message
        let result = await client.publish(
            'sensors/temperature',
            Buffer.from(JSON.stringify({ value: 23.5, unit: 'celsius' })),
            { contentType: 'application/json' }
        );
        console.log(`Published: message_id=${result.messageId}, subscribers=${result.subscriberCount}`);

        // Publish with retain
        result = await client.publish(
            'config/settings',
            Buffer.from(JSON.stringify({ theme: 'dark', language: 'en' })),
            { contentType: 'application/json', retain: true }
        );
        console.log(`Published retained: message_id=${result.messageId}`);

        // Publish multiple messages
        for (let i = 0; i < 5; i++) {
            result = await client.publish(
                'sensors/humidity',
                Buffer.from(JSON.stringify({ value: 45 + i, unit: '%' })),
                { contentType: 'application/json' }
            );
            console.log(`Published humidity #${i}: subscribers=${result.subscriberCount}`);
            await new Promise((resolve) => setTimeout(resolve, 500));
        }

        console.log('\nDone!');
    } finally {
        client.close();
    }
}

main().catch(console.error);
