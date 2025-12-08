using System.Runtime.CompilerServices;
using System.Text;
using Grpc.Net.Client;
using Nexusd.Sidecar;

namespace NexusdClient;

/// <summary>
/// A received message from a subscription.
/// </summary>
public record Message(
    string MessageId,
    string Topic,
    byte[] Payload,
    string ContentType,
    long TimestampMs,
    string SourceNodeId,
    string CorrelationId,
    bool IsRetained
)
{
    /// <summary>
    /// Get payload as a UTF-8 string.
    /// </summary>
    public string PayloadAsString => Encoding.UTF8.GetString(Payload);
}

/// <summary>
/// Result of a publish operation.
/// </summary>
public record PublishResult(
    bool Success,
    string MessageId,
    int SubscriberCount,
    string ErrorMessage
);

/// <summary>
/// Options for subscribe operations.
/// </summary>
public class SubscribeOptions
{
    public string ClientId { get; set; } = "";
    public bool ReceiveRetained { get; set; } = true;
    public int MaxBufferSize { get; set; } = 0;
}

/// <summary>
/// Topic information.
/// </summary>
public record TopicInfo(
    string Topic,
    int SubscriberCount,
    bool HasRetained
);

/// <summary>
/// Mesh peer information.
/// </summary>
public record PeerInfo(
    string NodeId,
    string Address,
    string ClusterId,
    bool IsHealthy
);

/// <summary>
/// NexusD Client for the pub/sub sidecar daemon.
/// </summary>
/// <example>
/// <code>
/// using var client = new Client("localhost:5672");
/// await client.PublishAsync("topic", "hello"u8.ToArray());
/// 
/// await foreach (var msg in client.SubscribeAsync(["topic"]))
/// {
///     Console.WriteLine($"{msg.Topic}: {msg.PayloadAsString}");
/// }
/// </code>
/// </example>
public class Client : IDisposable
{
    private readonly GrpcChannel _channel;
    private readonly SidecarService.SidecarServiceClient _client;
    private readonly string _clientId;

    /// <summary>
    /// Create a new NexusD client.
    /// </summary>
    /// <param name="address">Daemon address in "host:port" format</param>
    public Client(string address = "localhost:5672")
    {
        var uri = address.StartsWith("http") ? address : $"http://{address}";
        _channel = GrpcChannel.ForAddress(uri);
        _client = new SidecarService.SidecarServiceClient(_channel);
        _clientId = Guid.NewGuid().ToString();
    }

    /// <summary>
    /// Publish a message to a topic.
    /// </summary>
    public async Task<PublishResult> PublishAsync(
        string topic,
        byte[] payload,
        string contentType = "",
        bool retain = false,
        string correlationId = "",
        long ttlMs = 0,
        CancellationToken ct = default)
    {
        var request = new PublishRequest
        {
            Topic = topic,
            Payload = Google.Protobuf.ByteString.CopyFrom(payload),
            ContentType = contentType,
            Retain = retain,
            CorrelationId = correlationId,
            TtlMs = ttlMs
        };

        var response = await _client.PublishAsync(request, cancellationToken: ct);

        return new PublishResult(
            response.Success,
            response.MessageId,
            response.SubscriberCount,
            response.ErrorMessage
        );
    }

    /// <summary>
    /// Publish a string message to a topic.
    /// </summary>
    public Task<PublishResult> PublishAsync(
        string topic,
        string payload,
        string contentType = "",
        bool retain = false,
        string correlationId = "",
        long ttlMs = 0,
        CancellationToken ct = default)
    {
        return PublishAsync(
            topic,
            Encoding.UTF8.GetBytes(payload),
            contentType,
            retain,
            correlationId,
            ttlMs,
            ct
        );
    }

    /// <summary>
    /// Subscribe to topics and receive messages as an async stream.
    /// </summary>
    public async IAsyncEnumerable<Message> SubscribeAsync(
        IEnumerable<string> topics,
        SubscribeOptions? options = null,
        [EnumeratorCancellation] CancellationToken ct = default)
    {
        options ??= new SubscribeOptions();

        var request = new SubscribeRequest
        {
            ClientId = string.IsNullOrEmpty(options.ClientId) ? _clientId : options.ClientId,
            ReceiveRetained = options.ReceiveRetained,
            MaxBufferSize = options.MaxBufferSize
        };
        request.Topics.AddRange(topics);

        using var call = _client.Subscribe(request, cancellationToken: ct);

        await foreach (var eventMsg in call.ResponseStream.ReadAllAsync(ct))
        {
            Message? message = eventMsg.EventCase switch
            {
                MessageEvent.EventOneofCase.Message => new Message(
                    eventMsg.Message.MessageId,
                    eventMsg.Message.Topic,
                    eventMsg.Message.Payload.ToByteArray(),
                    eventMsg.Message.ContentType,
                    eventMsg.Message.TimestampMs,
                    eventMsg.Message.SourceNodeId,
                    eventMsg.Message.CorrelationId,
                    IsRetained: false
                ),
                MessageEvent.EventOneofCase.RetainedMessage => new Message(
                    eventMsg.RetainedMessage.MessageId,
                    eventMsg.RetainedMessage.Topic,
                    eventMsg.RetainedMessage.Payload.ToByteArray(),
                    eventMsg.RetainedMessage.ContentType,
                    eventMsg.RetainedMessage.TimestampMs,
                    eventMsg.RetainedMessage.SourceNodeId,
                    eventMsg.RetainedMessage.CorrelationId,
                    IsRetained: true
                ),
                // Ignore subscription_info and heartbeat
                _ => null
            };

            if (message != null)
            {
                yield return message;
            }
        }
    }

    /// <summary>
    /// Unsubscribe from a subscription.
    /// </summary>
    public async Task<bool> UnsubscribeAsync(string subscriptionId, CancellationToken ct = default)
    {
        var request = new UnsubscribeRequest { SubscriptionId = subscriptionId };
        var response = await _client.UnsubscribeAsync(request, cancellationToken: ct);
        return response.Success;
    }

    /// <summary>
    /// Get list of topics with subscriber counts.
    /// </summary>
    public async Task<IReadOnlyList<TopicInfo>> GetTopicsAsync(CancellationToken ct = default)
    {
        var request = new GetTopicsRequest();
        var response = await _client.GetTopicsAsync(request, cancellationToken: ct);
        
        return response.Topics
            .Select(t => new TopicInfo(t.Topic, t.SubscriberCount, t.HasRetained))
            .ToList();
    }

    /// <summary>
    /// Get list of mesh peers.
    /// </summary>
    public async Task<IReadOnlyList<PeerInfo>> GetPeersAsync(CancellationToken ct = default)
    {
        var request = new GetPeersRequest();
        var response = await _client.GetPeersAsync(request, cancellationToken: ct);
        
        return response.Peers
            .Select(p => new PeerInfo(p.NodeId, p.Address, p.ClusterId, p.IsHealthy))
            .ToList();
    }

    /// <summary>
    /// Dispose the client and close the channel.
    /// </summary>
    public void Dispose()
    {
        _channel.Dispose();
        GC.SuppressFinalize(this);
    }
}
