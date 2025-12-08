# NexusD C++ Client

C++ client library for the NexusD pub/sub sidecar daemon.

## Building

```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build -j

# Run examples
./build/publish_example
./build/subscribe_example
```

## Usage

### Basic Publish/Subscribe

```cpp
#include <nexusd_client/client.hpp>
#include <iostream>

int main() {
    // Connect to daemon
    nexusd::Client client("localhost:5672");
    
    // Publish a message
    auto result = client.publish("sensors/temp", "23.5");
    std::cout << "Published to " << result.subscriber_count << " subscribers\n";
    
    // Subscribe to topics
    client.subscribe({"sensors/temp", "sensors/humidity"},
        [](const nexusd::Message& msg) {
            std::cout << "Received: " << msg.topic << " -> " 
                      << std::string(msg.payload.begin(), msg.payload.end()) << "\n";
        }
    );
    
    return 0;
}
```

### With Options

```cpp
// Publish with retain
client.publish("config/settings", "{\"theme\":\"dark\"}", {
    .content_type = "application/json",
    .retain = true,
});

// Subscribe with options
client.subscribe({"sensors/#"}, callback, {
    .receive_retained = true,
    .max_buffer_size = 100,
});
```

## API Reference

### Client

```cpp
class Client {
public:
    Client(const std::string& address = "localhost:5672");
    ~Client();
    
    PublishResult publish(const std::string& topic, 
                          const std::vector<uint8_t>& payload,
                          const PublishOptions& opts = {});
    
    void subscribe(const std::vector<std::string>& topics,
                   MessageCallback callback,
                   const SubscribeOptions& opts = {});
    
    void unsubscribe(const std::string& subscription_id);
    
    std::vector<TopicInfo> getTopics();
    std::vector<PeerInfo> getPeers();
};
```

### Types

```cpp
struct Message {
    std::string message_id;
    std::string topic;
    std::vector<uint8_t> payload;
    std::string content_type;
    int64_t timestamp_ms;
    std::string source_node_id;
    std::string correlation_id;
    bool is_retained;
};

struct PublishResult {
    bool success;
    std::string message_id;
    int32_t subscriber_count;
    std::string error_message;
};
```

## Requirements

- C++17 compiler
- CMake 3.16+
- gRPC (auto-fetched if not found)
