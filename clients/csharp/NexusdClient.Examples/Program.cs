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
    using var client = new Client("localhost:5672");
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
    using var client = new Client("localhost:5672");
    Console.WriteLine("Connected to NexusD");
    Console.WriteLine("Subscribing to sensors/* topics...");
    Console.WriteLine("Press Ctrl+C to stop\n");

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
        var options = new SubscribeOptions { ReceiveRetained = true };

        await foreach (var message in client.SubscribeAsync(topics, options, cts.Token))
        {
            var retainedFlag = message.IsRetained ? " [RETAINED]" : "";
            Console.WriteLine($"Topic: {message.Topic}{retainedFlag}");
            Console.WriteLine($"  Payload: {message.PayloadAsString}");
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
