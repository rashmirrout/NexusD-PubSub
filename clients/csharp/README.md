# NexusD C# Client

C# client library for the NexusD pub/sub sidecar daemon.

## Installation

```bash
# NuGet (when published)
dotnet add package NexusdClient

# From source
dotnet build
```

## Building

```bash
cd clients/csharp
dotnet build
dotnet run --project NexusdClient.Examples
```

## Usage

### Basic Publish/Subscribe

```csharp
using NexusdClient;

// Connect to daemon
using var client = new NexusdClient.Client("localhost:5672");

// Publish a message
var result = await client.PublishAsync(
    "sensors/temperature",
    """{"value": 23.5}""",
    contentType: "application/json"
);
Console.WriteLine($"Published to {result.SubscriberCount} subscribers");

// Subscribe to topics
await foreach (var message in client.SubscribeAsync(["sensors/temperature"]))
{
    Console.WriteLine($"Received: {message.Topic} -> {message.PayloadAsString}");
}
```

### With Options

```csharp
// Publish with retain
await client.PublishAsync(
    "config/settings",
    """{"theme": "dark"}""",
    contentType: "application/json",
    retain: true
);

// Subscribe with retained messages
var options = new SubscribeOptions { ReceiveRetained = true };
await foreach (var msg in client.SubscribeAsync(["config/#"], options))
{
    Console.WriteLine(msg.IsRetained ? "[RETAINED] " : "" + msg.Topic);
}
```

## API Reference

### Client

```csharp
public class Client : IDisposable
{
    public Client(string address = "localhost:5672");
    
    public Task<PublishResult> PublishAsync(
        string topic,
        byte[] payload,
        string contentType = "",
        bool retain = false,
        string correlationId = "",
        long ttlMs = 0,
        CancellationToken ct = default);
    
    public IAsyncEnumerable<Message> SubscribeAsync(
        IEnumerable<string> topics,
        SubscribeOptions? options = null,
        CancellationToken ct = default);
    
    public Task<bool> UnsubscribeAsync(string subscriptionId, CancellationToken ct = default);
    
    public Task<IReadOnlyList<TopicInfo>> GetTopicsAsync(CancellationToken ct = default);
    
    public Task<IReadOnlyList<PeerInfo>> GetPeersAsync(CancellationToken ct = default);
}
```

### Types

```csharp
public record Message(
    string MessageId,
    string Topic,
    byte[] Payload,
    string ContentType,
    long TimestampMs,
    string SourceNodeId,
    string CorrelationId,
    bool IsRetained
) {
    public string PayloadAsString => Encoding.UTF8.GetString(Payload);
}

public record PublishResult(
    bool Success,
    string MessageId,
    int SubscriberCount,
    string ErrorMessage
);
```

## Requirements

- .NET 8.0+
- NexusD daemon running on target address
