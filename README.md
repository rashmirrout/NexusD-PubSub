# NexusD - Distributed Mesh Pub/Sub Sidecar Daemon

NexusD is a brokerless, distributed publish/subscribe middleware implementing the sidecar pattern. It enables automatic peer discovery via UDP multicast, hash-triggered gossip synchronization, and gRPC-based data plane operations.

## Vision

**Reimagining Cloud-Native Messaging for the Edge-to-Cloud Era**

NexusD aims to be the **fastest, most resilient, and operationally simplest** publish/subscribe middleware for modern distributed systems. By eliminating the central broker bottleneck and embracing a **true peer-to-peer mesh architecture**, we're building the messaging fabric for the next generation of cloud-native applications.

🚀 **Our Mission:**
- **Sub-millisecond local delivery** - Zero broker hops means <1ms latency for co-located publishers and subscribers
- **Linear horizontal scalability** - Add nodes to scale throughput, not just availability
- **Self-healing mesh** - No single point of failure; automatic peer discovery and routing table convergence
- **Zero-ops deployment** - Sidecar pattern means no external infrastructure to manage, upgrade, or monitor
- **Polyglot ecosystem** - First-class client libraries for Python, Rust, C++, C#, Node.js, and beyond

🎯 **Target Use Cases:**
- **Microservices event buses** replacing heavyweight brokers with 90%+ cost reduction
- **IoT edge computing** with millions of sensors and millisecond response times
- **Real-time analytics pipelines** processing 100K+ events/second per node
- **Multi-tenant SaaS platforms** with namespace isolation and per-tenant routing
- **Kubernetes-native applications** leveraging StatefulSet DNS and headless services

---

![NexusD Architecture](docs/NexusD.png)

## Architecture Overview

```
┌───────────────────────────────────────────────────────────────────────────┐
│                              NexusD Mesh                                   │
│                                                                            │
│   ┌─────────────┐       ┌─────────────┐       ┌─────────────┐            │
│   │   Node A    │◄─────►│   Node B    │◄─────►│   Node C    │            │
│   │  (Sidecar)  │       │  (Sidecar)  │       │  (Sidecar)  │            │
│   └──────┬──────┘       └──────┬──────┘       └──────┬──────┘            │
│          │                     │                     │                    │
│          │ gRPC                │ gRPC                │ gRPC              │
│          ▼                     ▼                     ▼                    │
│   ┌─────────────┐       ┌─────────────┐       ┌─────────────┐            │
│   │    App 1    │       │    App 2    │       │    App 3    │            │
│   └─────────────┘       └─────────────┘       └─────────────┘            │
│                                                                            │
│   Discovery: UDP Multicast (239.255.42.1:5670)                            │
│   Mesh Sync: gRPC (port 5671)                                             │
│   App API:   gRPC (port 5672)                                             │
└───────────────────────────────────────────────────────────────────────────┘
```

## Features

- **Brokerless Design**: No central message broker required; every node is equal
- **UDP Multicast Discovery**: Automatic peer discovery with 1Hz beacon heartbeats
- **Hash-Triggered Gossip**: Sync only when topic subscription state changes (CRC64)
- **gRPC Data Plane**: Efficient async callback API for pub/sub operations
- **Retained Messages**: Last-known-good values delivered to new subscribers
- **Cross-Platform**: Windows (Winsock2) and Linux/macOS (POSIX sockets)
- **Client Resilience**: Automatic reconnection with exponential backoff across all client libraries
- **Gap Detection**: Sequence number tracking to detect and recover missed messages
- **Subscription Recovery**: Pause/resume subscriptions with message replay from ring buffer

## Building

### Prerequisites

- CMake 3.16 or higher
- C++17 compatible compiler (GCC 8+, Clang 7+, MSVC 2019+)
- gRPC v1.60.0 (auto-fetched via FetchContent if not found)

### Build Commands

```bash
# Create build directory
mkdir build && cd build

# Configure (Debug or Release)
cmake -DCMAKE_BUILD_TYPE=Release ..

# Build all targets
cmake --build . --config Release -j

# Run tests
ctest --output-on-failure
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `NEXUSD_BUILD_TESTS` | ON | Build the test suite |
| `NEXUSD_BUILD_CLI` | ON | Build the CLI tool |
| `NEXUSD_BUILD_CPP_CLIENT` | ON | Build the C++ client library |
| `NEXUSD_FETCH_GRPC` | ON | Fetch gRPC via FetchContent if not found |
| `BUILD_SHARED_LIBS` | ON | Build shared libraries (.so/.dll) |

**Example:** Build only the daemon without tests or CLI:
```bash
cmake -DNEXUSD_BUILD_TESTS=OFF -DNEXUSD_BUILD_CLI=OFF -DNEXUSD_BUILD_CPP_CLIENT=OFF ..
cmake --build . -j
```

### Output Structure

After building:
```
build/
├── bin/
│   ├── nexusd              # Main daemon executable
│   └── tests/              # Test executables
└── lib/
    ├── libnexusd_utils.so  # Utility library
    ├── libnexusd_net.so    # Network abstraction
    ├── libnexusd_proto.so  # Protobuf/gRPC stubs
    ├── libnexusd_core.so   # Core logic (registry, discovery)
    └── libnexusd_services.so # gRPC services
```

## Usage

### Starting a Node

```bash
# Start with defaults
./build/bin/nexusd

# Custom configuration
./build/bin/nexusd \
    --cluster production \
    --mcast-addr 239.255.42.1 \
    --mcast-port 5670 \
    --mesh-port 5671 \
    --app-port 5672 \
    --log-level DEBUG
```

### Command Line Options

| Option | Default | Description |
|--------|---------|-------------|
| `--cluster <id>` | default | Cluster identifier (nodes only discover within same cluster) |
| `--mcast-addr <addr>` | 239.255.42.1 | Multicast address for discovery |
| `--mcast-port <port>` | 5670 | Multicast port for discovery |
| `--mesh-port <port>` | 5671 | gRPC port for mesh communication |
| `--app-port <port>` | 5672 | gRPC port for application sidecar API |
| `--bind <addr>` | 0.0.0.0 | Bind address for gRPC servers |
| `--log-level <level>` | INFO | TRACE, DEBUG, INFO, WARN, ERROR, FATAL |

### Client API (gRPC)

Applications connect to `localhost:5672` and use the SidecarService:

```protobuf
service SidecarService {
    // Publish a message to a topic
    rpc Publish(PublishRequest) returns (PublishResponse);
    
    // Subscribe to topics (server-streaming)
    rpc Subscribe(SubscribeRequest) returns (stream Message);
    
    // Unsubscribe from topics
    rpc Unsubscribe(UnsubscribeRequest) returns (UnsubscribeResponse);
}
```

## Library Modules

| Library | Description | Dependencies |
|---------|-------------|--------------|
| `nexusd_utils` | Logger, UUID, CRC64 | None |
| `nexusd_net` | Cross-platform UDP socket | `nexusd_utils` |
| `nexusd_proto` | Generated protobuf/gRPC code | gRPC |
| `nexusd_core` | PeerRegistry, DiscoveryAgent | `nexusd_utils`, `nexusd_net`, `nexusd_proto` |
| `nexusd_services` | MeshService, SidecarService | `nexusd_core`, `nexusd_proto`, gRPC |

## Protocol Details

### Discovery Beacon (UDP Multicast)

Every node broadcasts a beacon at 1Hz containing:
- Instance UUID (unique per process)
- Cluster ID (isolation boundary)
- RPC IP and port for mesh communication
- Topic state hash (CRC64 of subscribed topics)

### Hash-Triggered Gossip Sync

When a node detects a peer with a different `topic_state_hash`, it initiates a `GetNodeState` RPC to fetch the peer's subscriptions and update the local routing table.

### Message Routing

1. **Local delivery**: Messages published to a topic are delivered to all local subscribers
2. **Mesh forwarding**: If remote nodes subscribe to the topic, messages are pushed via `PushMessage` RPC

## Tools

### CLI

Command-line debugging and management tool for NexusD instances.

```bash
nexusd-cli DISCOVER          # Find instances
nexusd-cli -n 1              # Connect to first instance
nexusd-cli INFO              # Get server info
nexusd-cli MESH PEERS        # List mesh peers
```

See [tools/cli/README.md](tools/cli/README.md) for full documentation.

### Demo Cluster

Scripts to run a local multi-node cluster for testing.

```bash
cd tools/demo
./start.sh      # Start 3-node cluster
./stop.sh       # Stop cluster
```

See [tools/demo/README.md](tools/demo/README.md) for configuration options.

## Client Libraries

Multi-language client SDKs for integrating with NexusD.

| Language | Package | Documentation |
|----------|---------|---------------|
| Python | `nexusd-client` | [clients/python/README.md](clients/python/README.md) |
| C++ | `libnexusd_client` | [clients/cpp/README.md](clients/cpp/README.md) |
| Rust | `nexusd-client` | [clients/rust/README.md](clients/rust/README.md) |
| C# | `NexusdClient` | [clients/csharp/README.md](clients/csharp/README.md) |
| Node.js | `nexusd-client` | [clients/node/README.md](clients/node/README.md) |

## Documentation

- [Architecture Overview](docs/ARCHITECTURE.md) - System design and components
- [Design Details](docs/design.md) - Gossip protocol, threading model
- [Docker Deployment](docs/DOCKER.md) - Container build and docker-compose setup
- [Kubernetes Deployment](docs/KUBERNETES.md) - StatefulSet deployment with multicast workarounds
- [Real-World Use Cases](docs/USE_CASES.md) - Production examples: E-commerce, IoT, SaaS platforms
- [Production Roadmap](docs/ROADMAP.md) - Path to cloud-native production readiness

## License

MIT License - see [LICENSE](LICENSE) for details.

## Contributing

💡 **Join the Brokerless Revolution!**

We're building the **broker-killer** - a system that combines the simplicity of Redis Pub/Sub, the performance of shared memory, and the resilience of gossip protocols. Whether you're passionate about **high-performance networking**, **distributed systems theory**, **cloud-native architecture**, or **developer experience**, there's a place for you here.

🌟 **We're actively seeking contributors for:**
- **Observability**: Prometheus metrics integration, OpenTelemetry tracing, structured logging
- **Cloud-Native**: Kubernetes Operator, DNS-based discovery, Helm charts
- **Security**: TLS/mTLS implementation, topic ACLs, rate limiting
- **Performance**: Zero-copy optimizations, benchmarking suite, scalability testing
- **Client SDKs**: Language bindings, examples, documentation
- **Operations**: Grafana dashboards, Terraform modules, deployment guides
- **Core Features**: Topic wildcards, QoS levels, persistent WAL, circuit breakers

Every PR brings us closer to making **brokerless pub/sub** the new standard for cloud-native messaging. See [ROADMAP.md](docs/ROADMAP.md) for the full feature roadmap.

**How to Contribute:**

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Make your changes with tests
4. Ensure all tests pass (`ctest --output-on-failure`)
5. Submit a pull request

**Resources for Contributors:**
- [ARCHITECTURE.md](docs/ARCHITECTURE.md) - Comprehensive system design with Mermaid diagrams
- [ROADMAP.md](docs/ROADMAP.md) - 7-phase production roadmap with implementation details
- [USE_CASES.md](docs/USE_CASES.md) - Real-world examples to guide feature development

Questions? Open an issue or start a discussion. We're here to help! 🚀
