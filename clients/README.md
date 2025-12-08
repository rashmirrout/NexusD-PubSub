# NexusD Client Libraries

Multi-language client libraries for communicating with the NexusD pub/sub sidecar daemon.

## Overview

All clients connect to the NexusD daemon's SidecarService (default: `127.0.0.1:5672`) and provide:

- **Publish**: Send messages to topics
- **Subscribe**: Receive streaming messages from topics
- **Unsubscribe**: Cancel subscriptions

## Available Clients

| Language | Directory | Status |
|----------|-----------|--------|
| Python | [python/](python/) | ✅ Ready |
| C++ | [cpp/](cpp/) | ✅ Ready |
| Rust | [rust/](rust/) | ✅ Ready |
| C# | [csharp/](csharp/) | ✅ Ready |
| Node.js | [node/](node/) | ✅ Ready |

## Proto Source

All clients compile from the shared proto file at `../proto/sidecar.proto`. No symlinks or copies needed—each build tool references the proto via relative path.

## Common API Pattern

All clients expose a similar high-level API:

```
client = NexusdClient("localhost:5672")
client.connect()

# Publish
client.publish("sensors/temp", b'{"value": 23.5}')

# Subscribe (streaming)
for message in client.subscribe(["sensors/temp", "sensors/humidity"]):
    print(message.topic, message.payload)

# Cleanup
client.close()
```

## Building

Each client has its own build system. See individual READMEs:

- **Python**: `pip install -e .` or `uv pip install -e .`
- **C++**: `cmake -B build && cmake --build build`
- **Rust**: `cargo build`
- **C#**: `dotnet build`
- **Node.js**: `npm install && npm run build`

## Requirements

- NexusD daemon running on localhost:5672
- Language-specific gRPC runtime (installed automatically by each package manager)
