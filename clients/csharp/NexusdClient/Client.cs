using System.Runtime.CompilerServices;
using System.Text;
using Grpc.Net.Client;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Nexusd.Sidecar;

namespace NexusdClient;

/// <summary>
/// Gap recovery mode for ResumeSubscribe.
/// </summary>
public enum GapRecoveryMode
{
    None = 0,
    RetainedOnly = 1,
    ReplayBuffer = 2
}

/// <summary>
/// Configuration for automatic reconnection.
/// </summary>
public class ReconnectConfig
{
    public bool Enabled { get; set; } = true;
    public TimeSpan InitialDelay { get; set; } = TimeSpan.FromMilliseconds(100);
    public TimeSpan MaxDelay { get; set; } = TimeSpan.FromSeconds(30);
    public double Multiplier { get; set; } = 2.0;
    public int MaxAttempts { get; set; } = 0; // 0 = unlimited
}

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
    bool IsRetained,
    bool IsReplay = false,
    ulong SequenceNumber = 0
)
{
    /// <summary>
    /// Get payload as a UTF-8 string.
    /// </summary>
    public string PayloadAsString => Encoding.UTF8.GetString(Payload);
}

/// <summary>
/// Information about an active subscription.
/// </summary>
public record SubscriptionInfo(
    string SubscriptionId,
    IReadOnlyList<string> Topics,
    bool GapDetected,
    ulong MissedMessageCount,
    bool ReplayStarted,
    ulong LastSequence
);

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
    public GapRecoveryMode GapRecoveryMode { get; set; } = GapRecoveryMode.ReplayBuffer;
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
/// Features automatic reconnection with exponential backoff,
/// gap detection, and message replay.
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
    private readonly string _address;
    private GrpcChannel _channel;
    private SidecarService.SidecarServiceClient _client;
    private readonly string _clientId;
    private readonly ReconnectConfig _reconnectConfig;
    private readonly ILogger<Client> _logger;
    private readonly Dictionary<string, SubscriptionInfo> _activeSubscriptions = new();
    private readonly object _subscriptionLock = new();

    /// <summary>
    /// Create a new NexusD client.
    /// </summary>
    /// <param name="address">Daemon address in "host:port" format</param>
    /// <param name="reconnectConfig">Reconnection configuration</param>
    /// <param name="logger">Optional logger</param>
    public Client(
        string address = "localhost:5672",
        ReconnectConfig? reconnectConfig = null,
        ILogger<Client>? logger = null)
    {
        _address = address.StartsWith("http") ? address : $"http://{address}";
        _channel = GrpcChannel.ForAddress(_address);
        _client = new SidecarService.SidecarServiceClient(_channel);
        _clientId = Guid.NewGuid().ToString();
        _reconnectConfig = reconnectConfig ?? new ReconnectConfig();
        _logger = logger ?? NullLogger<Client>.Instance;
        
        _logger.LogInformation("NexusD client initialized for {Address}", address);
    }

    private async Task ReconnectAsync(CancellationToken ct)
    {
        if (!_reconnectConfig.Enabled)
            throw new InvalidOperationException("Reconnection is disabled");

        var delay = _reconnectConfig.InitialDelay;
        var attempt = 0;

        while (true)
        {
            attempt++;
            if (_reconnectConfig.MaxAttempts > 0 && attempt > _reconnectConfig.MaxAttempts)
            {
                _logger.LogError("Max reconnection attempts {MaxAttempts} exceeded", _reconnectConfig.MaxAttempts);
                throw new InvalidOperationException("Max reconnection attempts exceeded");
            }

            _logger.LogInformation("Reconnection attempt {Attempt} with delay {Delay}ms", 
                attempt, delay.TotalMilliseconds);

            await Task.Delay(delay, ct);

            try
            {
                _channel.Dispose();
                _channel = GrpcChannel.ForAddress(_address);
                _client = new SidecarService.SidecarServiceClient(_channel);
                _logger.LogInformation("Reconnection successful on attempt {Attempt}", attempt);
                return;
            }
            catch (Exception ex)
            {
                _logger.LogWarning(ex, "Reconnection attempt {Attempt} failed", attempt);
            }

            delay = TimeSpan.FromMilliseconds(Math.Min(
                delay.TotalMilliseconds * _reconnectConfig.Multiplier,
                _reconnectConfig.MaxDelay.TotalMilliseconds));
        }
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

        try
        {
            var response = await _client.PublishAsync(request, cancellationToken: ct);
            _logger.LogDebug("Published to {Topic}, message_id={MessageId}", topic, response.MessageId);

            return new PublishResult(
                response.Success,
                response.MessageId,
                response.SubscriberCount,
                response.ErrorMessage
            );
        }
        catch (Grpc.Core.RpcException ex) when (ex.StatusCode == Grpc.Core.StatusCode.Unavailable)
        {
            _logger.LogWarning("Connection lost during publish, reconnecting...");
            await ReconnectAsync(ct);
            var response = await _client.PublishAsync(request, cancellationToken: ct);
            return new PublishResult(
                response.Success,
                response.MessageId,
                response.SubscriberCount,
                response.ErrorMessage
            );
        }
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
    /// Features automatic reconnection with gap detection.
    /// </summary>
    /// <param name="topics">Topics to subscribe to</param>
    /// <param name="options">Subscribe options</param>
    /// <param name="onGapDetected">Callback when gap is detected</param>
    /// <param name="ct">Cancellation token</param>
    public async IAsyncEnumerable<Message> SubscribeAsync(
        IEnumerable<string> topics,
        SubscribeOptions? options = null,
        Action<SubscriptionInfo>? onGapDetected = null,
        [EnumeratorCancellation] CancellationToken ct = default)
    {
        options ??= new SubscribeOptions();
        var topicList = topics.ToList();
        string? subscriptionId = null;
        ulong lastSequence = 0;

        while (!ct.IsCancellationRequested)
        {
            Grpc.Core.AsyncServerStreamingCall<MessageEvent>? call = null;
            
            try
            {
                if (subscriptionId != null && _reconnectConfig.Enabled)
                {
                    // Resume with gap recovery
                    _logger.LogInformation("Resuming subscription {SubscriptionId} from sequence {Sequence}",
                        subscriptionId, lastSequence);

                    var resumeRequest = new ResumeSubscribeRequest
                    {
                        SubscriptionId = subscriptionId,
                        LastSequenceNumber = lastSequence,
                        GapRecoveryMode = (Nexusd.Sidecar.GapRecoveryMode)options.GapRecoveryMode
                    };
                    call = _client.ResumeSubscribe(resumeRequest, cancellationToken: ct);
                }
                else
                {
                    // Fresh subscription
                    var request = new SubscribeRequest
                    {
                        ClientId = string.IsNullOrEmpty(options.ClientId) ? _clientId : options.ClientId,
                        ReceiveRetained = options.ReceiveRetained,
                        MaxBufferSize = options.MaxBufferSize
                    };
                    request.Topics.AddRange(topicList);
                    call = _client.Subscribe(request, cancellationToken: ct);
                }

                await foreach (var eventMsg in call.ResponseStream.ReadAllAsync(ct))
                {
                    switch (eventMsg.EventCase)
                    {
                        case MessageEvent.EventOneofCase.SubscriptionInfo:
                            subscriptionId = eventMsg.SubscriptionInfo.SubscriptionId;
                            var subInfo = new SubscriptionInfo(
                                eventMsg.SubscriptionInfo.SubscriptionId,
                                eventMsg.SubscriptionInfo.Topics.ToList(),
                                eventMsg.SubscriptionInfo.GapDetected,
                                eventMsg.SubscriptionInfo.MissedMessageCount,
                                eventMsg.SubscriptionInfo.ReplayStarted,
                                lastSequence
                            );

                            lock (_subscriptionLock)
                            {
                                _activeSubscriptions[subscriptionId] = subInfo;
                            }

                            _logger.LogInformation("Subscription {Id} active, gap={Gap}, missed={Missed}",
                                subscriptionId, subInfo.GapDetected, subInfo.MissedMessageCount);

                            if (subInfo.GapDetected && onGapDetected != null)
                            {
                                onGapDetected(subInfo);
                            }
                            break;

                        case MessageEvent.EventOneofCase.Message:
                            lastSequence = eventMsg.Message.SequenceNumber;
                            yield return new Message(
                                eventMsg.Message.MessageId,
                                eventMsg.Message.Topic,
                                eventMsg.Message.Payload.ToByteArray(),
                                eventMsg.Message.ContentType,
                                eventMsg.Message.TimestampMs,
                                eventMsg.Message.SourceNodeId,
                                eventMsg.Message.CorrelationId,
                                IsRetained: false,
                                IsReplay: false,
                                SequenceNumber: eventMsg.Message.SequenceNumber
                            );
                            break;

                        case MessageEvent.EventOneofCase.RetainedMessage:
                            yield return new Message(
                                eventMsg.RetainedMessage.MessageId,
                                eventMsg.RetainedMessage.Topic,
                                eventMsg.RetainedMessage.Payload.ToByteArray(),
                                eventMsg.RetainedMessage.ContentType,
                                eventMsg.RetainedMessage.TimestampMs,
                                eventMsg.RetainedMessage.SourceNodeId,
                                eventMsg.RetainedMessage.CorrelationId,
                                IsRetained: true
                            );
                            break;

                        case MessageEvent.EventOneofCase.ReplayMessage:
                            lastSequence = Math.Max(lastSequence, eventMsg.ReplayMessage.SequenceNumber);
                            yield return new Message(
                                eventMsg.ReplayMessage.MessageId,
                                eventMsg.ReplayMessage.Topic,
                                eventMsg.ReplayMessage.Payload.ToByteArray(),
                                "",
                                eventMsg.ReplayMessage.TimestampMs,
                                eventMsg.ReplayMessage.SourceNodeId,
                                "",
                                IsRetained: false,
                                IsReplay: true,
                                SequenceNumber: eventMsg.ReplayMessage.SequenceNumber
                            );
                            break;

                        case MessageEvent.EventOneofCase.ReplayComplete:
                            _logger.LogInformation("Replay complete for subscription {Id}", subscriptionId);
                            break;
                    }
                }

                // Stream ended normally
                break;
            }
            catch (Grpc.Core.RpcException ex) when (ex.StatusCode == Grpc.Core.StatusCode.Cancelled)
            {
                _logger.LogDebug("Subscription cancelled");
                break;
            }
            catch (Grpc.Core.RpcException ex) when (ex.StatusCode == Grpc.Core.StatusCode.Unavailable)
            {
                _logger.LogWarning("Connection lost during subscription, reconnecting...");
                await ReconnectAsync(ct);
                // Loop will retry with ResumeSubscribe
            }
        }

        // Cleanup
        if (subscriptionId != null)
        {
            lock (_subscriptionLock)
            {
                _activeSubscriptions.Remove(subscriptionId);
            }
        }
    }

    /// <summary>
    /// Unsubscribe from a subscription.
    /// </summary>
    /// <param name="subscriptionId">Subscription ID to cancel</param>
    /// <param name="pause">If true, pause for later resumption</param>
    /// <param name="ct">Cancellation token</param>
    public async Task<bool> UnsubscribeAsync(
        string subscriptionId, 
        bool pause = false,
        CancellationToken ct = default)
    {
        var request = new UnsubscribeRequest 
        { 
            SubscriptionId = subscriptionId,
            Pause = pause
        };
        var response = await _client.UnsubscribeAsync(request, cancellationToken: ct);

        lock (_subscriptionLock)
        {
            _activeSubscriptions.Remove(subscriptionId);
        }

        _logger.LogInformation("Unsubscribed {Id}, pause={Pause}", subscriptionId, pause);
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
    /// Get active subscriptions.
    /// </summary>
    public IReadOnlyList<SubscriptionInfo> GetActiveSubscriptions()
    {
        lock (_subscriptionLock)
        {
            return _activeSubscriptions.Values.ToList();
        }
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
