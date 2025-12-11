using System.Text.Json;
using NexusdClient;

// Determine which example to run based on args
var mode = args.Length > 0 ? args[0] : "publish";

Console.WriteLine("NexusD C# Client Examples");
Console.WriteLine("=========================\n");

if (mode == "subscribe")
{
    await RunSubscribeExample();
}
else
{
    await RunPublishExample();
}

async Task RunPublishExample()
{
    // Configure client with reconnection options
    var options = new ClientOptions
    {
        MaxRetries = 10,
        InitialRetryDelayMs = 1000,
        MaxRetryDelayMs = 30000,
    };

    using var client = new Client("localhost:5672", options);
    Console.WriteLine("Connected to NexusD\n");

    // Publish a simple message
    var result = await client.PublishAsync(
        "sensors/temperature",
        JsonSerializer.Serialize(new { value = 23.5, unit = "celsius" }),
        contentType: "application/json"
    );
    Console.WriteLine($"Published: message_id={result.MessageId}, subscribers={result.SubscriberCount}");

    // Publish with retain
    result = await client.PublishAsync(
        "config/settings",
        JsonSerializer.Serialize(new { theme = "dark", language = "en" }),
        contentType: "application/json",
        retain: true
    );
    Console.WriteLine($"Published retained: message_id={result.MessageId}");

    // Publish multiple messages
    for (int i = 0; i < 5; i++)
    {
        result = await client.PublishAsync(
            "sensors/humidity",
            JsonSerializer.Serialize(new { value = 45 + i, unit = "%" }),
            contentType: "application/json"
        );
        Console.WriteLine($"Published humidity #{i}: subscribers={result.SubscriberCount}");
        await Task.Delay(500);
    }

    Console.WriteLine("\nDone!");
}

async Task RunSubscribeExample()
{
    // Configure client with reconnection options
    var clientOptions = new ClientOptions
    {
        MaxRetries = 0,  // 0 = infinite retries
        InitialRetryDelayMs = 1000,
        MaxRetryDelayMs = 30000,
    };

    using var client = new Client("localhost:5672", clientOptions);
    Console.WriteLine("Connected to NexusD");
    Console.WriteLine("Subscribing to sensors/* topics...");
    Console.WriteLine("Auto-reconnect enabled with gap recovery");
    Console.WriteLine("Press Ctrl+C to stop\n");

    // Register connection state callback
    client.OnConnectionStateChange += (state, error) =>
    {
        Console.WriteLine($"[Connection] State: {state}" + (error != null ? $" - {error.Message}" : ""));
    };

    // Register gap detection callback
    client.OnGapDetected += (expected, actual, topic) =>
    {
        var gap = actual - expected;
        Console.WriteLine($"[Gap] Detected {gap} missed messages on {topic}");
        Console.WriteLine($"       Expected seq {expected}, got {actual}");
    };

    var cts = new CancellationTokenSource();
    Console.CancelKeyPress += (_, e) =>
    {
        e.Cancel = true;
        cts.Cancel();
        Console.WriteLine("\nShutting down...");
    };

    try
    {
        string[] topics = ["sensors/temperature", "sensors/humidity", "config/settings"];
        
        // Subscribe with gap recovery enabled
        var options = new SubscribeOptions
        {
            ReceiveRetained = true,
            GapRecoveryMode = GapRecoveryMode.ReplayBuffer,
        };

        await foreach (var message in client.SubscribeAsync(topics, options, cts.Token))
        {
            var retainedFlag = message.IsRetained ? " [RETAINED]" : "";
            Console.WriteLine($"Topic: {message.Topic}{retainedFlag}");
            Console.WriteLine($"  Payload: {message.PayloadAsString}");
            Console.WriteLine($"  Sequence: {message.SequenceNumber}");
            Console.WriteLine($"  Message ID: {message.MessageId}");
            Console.WriteLine($"  Timestamp: {message.TimestampMs}");
            Console.WriteLine();
        }
    }
    catch (OperationCanceledException)
    {
        // Normal cancellation
    }
}
