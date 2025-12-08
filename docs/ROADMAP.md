# NexusD Production Roadmap

This document provides a comprehensive guide to transforming NexusD from a functional prototype into a production-grade, cloud-native distributed pub/sub system. Each phase includes detailed requirements, implementation guidance, and success criteria.

---

## Executive Summary

### Current State

NexusD v1.0 is a **functional brokerless pub/sub middleware** with these capabilities:

✅ **Core Functionality**
- UDP multicast peer discovery with 1Hz heartbeats
- Hash-triggered gossip synchronization (CRC64-based)
- gRPC data plane for publish/subscribe operations
- Retained message support (last-known-good values)
- Cross-platform networking (Windows Winsock2, Linux/macOS POSIX)

✅ **Developer Experience**
- Multi-language client SDKs (Python, C++, Rust, C#, Node.js)
- CLI debugging tool with discovery and mesh inspection
- Docker containerization with multi-stage builds
- Kubernetes deployment guides (StatefulSet with workarounds)
- Demo cluster scripts for local testing

### Suitability Assessment

| Use Case | Readiness | Notes |
|----------|-----------|-------|
| **Development & Testing** | ✅ Production Ready | Full feature set, good DX |
| **Small-Scale Deployments (3-10 nodes)** | ✅ Production Ready | On-premise, internal tools |
| **Medium-Scale Cloud (10-50 nodes)** | ⚠️ Partial | Needs observability, security |
| **Large-Scale Production (50+ nodes)** | ❌ Not Ready | Requires all roadmap phases |
| **Multi-Region Deployments** | ❌ Not Ready | Federation not implemented |
| **Regulated Industries (HIPAA, SOC2)** | ❌ Not Ready | Missing security features |

### Gap Analysis

**What's Missing for Production:**

1. **Discovery Limitations** - UDP multicast doesn't work in Kubernetes standard networking
2. **Observability Gap** - No metrics, tracing, structured logs, or health endpoints
3. **Security Gap** - No encryption, authentication, or authorization
4. **Resilience Gap** - No circuit breakers, backpressure handling, or graceful degradation
5. **Operational Gap** - No Helm charts, operators, or automated management

### Roadmap Timeline

| Phase | Duration | Dependencies | Outcome |
|-------|----------|--------------|---------|
| Phase 1: Cloud-Native Discovery | 6-8 weeks | None | Works on any K8s cluster |
| Phase 2: Observability | 8-10 weeks | Phase 1 | Production-grade monitoring |
| Phase 3: Security | 10-12 weeks | Phase 2 | Compliance-ready |
| Phase 4: Resilience | 6-8 weeks | Phase 2 | Self-healing mesh |
| Phase 5: Performance | 8-10 weeks | Phase 4 | High-throughput capable |
| Phase 6: Operations | 6-8 weeks | Phase 1-3 | Automated deployment |
| Phase 7: Advanced Features | 12-16 weeks | Phase 1-6 | Enterprise-grade |

**Total estimated timeline:** 56-72 weeks (~12-18 months) for full production readiness.

---

## Phase 1: Cloud-Native Discovery

**Goal:** Eliminate UDP multicast dependency and enable dynamic peer discovery in any environment.

**Business Value:** NexusD works on managed Kubernetes (EKS, GKE, AKS) without networking workarounds.

### 1.1 Built-in Peer Retry Logic

**Priority:** 🔴 Critical | **Effort:** Medium (2-3 weeks) | **Risk:** Low

#### Problem Statement

Current `--peers` flag requires all peer DNS names to be resolvable at startup. In Kubernetes:

1. StatefulSet creates `nexusd-0` (starts, DNS ready)
2. StatefulSet creates `nexusd-1` (starts, tries to connect to `nexusd-2:5671`)
3. DNS lookup fails because `nexusd-2` doesn't exist yet
4. `nexusd-1` crashes or starts with incomplete mesh

**Current workaround:** Init container with `nslookup` loop (adds complexity).

#### Proposed Solution

Add retry mechanism with exponential backoff:

```bash
nexusd \
  --peers "nexusd-0:5671,nexusd-1:5671,nexusd-2:5671" \
  --peer-retry-count 10 \
  --peer-retry-interval 2000 \
  --peer-retry-max-interval 60000
```

#### Implementation Details

**New CLI Flags:**

| Flag | Type | Default | Description |
|------|------|---------|-------------|
| `--peer-retry-count` | int | 10 | Max DNS resolution attempts per peer |
| `--peer-retry-interval` | int (ms) | 2000 | Initial delay between retries |
| `--peer-retry-max-interval` | int (ms) | 60000 | Max delay after exponential backoff |
| `--peer-retry-backoff-factor` | float | 2.0 | Multiplier for exponential backoff |
| `--peer-retry-jitter` | float | 0.2 | Random jitter to prevent thundering herd (0.0-1.0) |

**Code Changes:**

```cpp
// src/core/peer_connector.hpp
class PeerConnector {
public:
    struct RetryConfig {
        int max_attempts = 10;
        std::chrono::milliseconds initial_interval{2000};
        std::chrono::milliseconds max_interval{60000};
        double backoff_factor = 2.0;
        double jitter = 0.2;
    };

    void connectToPeers(
        const std::vector<std::string>& peers,
        const RetryConfig& retry_config = RetryConfig{}
    );

private:
    void connectWithRetry(
        const std::string& peer_address,
        const RetryConfig& config
    );
    
    std::chrono::milliseconds calculateBackoff(
        int attempt,
        const RetryConfig& config
    );
};

// src/core/peer_connector.cpp
void PeerConnector::connectWithRetry(
    const std::string& peer_address,
    const RetryConfig& config
) {
    for (int attempt = 0; attempt < config.max_attempts; ++attempt) {
        try {
            auto addr = dns_resolver_.resolve(peer_address);
            if (addr.has_value()) {
                connectTo(addr.value());
                logger_.info("Connected to peer {} after {} attempts", 
                           peer_address, attempt + 1);
                return;
            }
        } catch (const DNSException& e) {
            logger_.warn("DNS resolution failed for {} (attempt {}/{}): {}", 
                        peer_address, attempt + 1, config.max_attempts, e.what());
        }
        
        if (attempt < config.max_attempts - 1) {
            auto backoff = calculateBackoff(attempt, config);
            logger_.debug("Retrying {} in {}ms", peer_address, backoff.count());
            std::this_thread::sleep_for(backoff);
        }
    }
    
    logger_.error("Failed to connect to {} after {} attempts", 
                 peer_address, config.max_attempts);
}

std::chrono::milliseconds PeerConnector::calculateBackoff(
    int attempt,
    const RetryConfig& config
) {
    // Exponential backoff: initial * (factor ^ attempt)
    auto base_delay = config.initial_interval.count() * 
                     std::pow(config.backoff_factor, attempt);
    
    // Cap at max_interval
    base_delay = std::min(base_delay, 
                         static_cast<double>(config.max_interval.count()));
    
    // Add jitter to prevent thundering herd
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(1.0 - config.jitter, 
                                        1.0 + config.jitter);
    
    return std::chrono::milliseconds(
        static_cast<long long>(base_delay * dis(gen))
    );
}
```

**Testing Requirements:**

- [ ] Unit test: Verify exponential backoff calculation
- [ ] Unit test: Verify jitter randomization
- [ ] Integration test: Successful connection after 3 retries
- [ ] Integration test: Give up after max_attempts
- [ ] Integration test: Multiple peers with different availability
- [ ] Load test: 100 nodes starting simultaneously

**Documentation Updates:**

- Update `docs/KUBERNETES.md` - Remove init container, document retry flags
- Update `README.md` - Add retry flags to command line options table
- Add example to `tools/demo/` showing retry in action

**Success Criteria:**

- ✅ Kubernetes StatefulSet starts without init container
- ✅ All pods achieve full mesh connectivity within 60 seconds
- ✅ No crashes due to DNS resolution failures
- ✅ Logs show retry attempts at DEBUG level

---

### 1.2 Kubernetes-Native Discovery

**Priority:** 🔴 Critical | **Effort:** High (4-5 weeks) | **Risk:** Medium

#### Problem Statement

Static `--peers` list doesn't support:
- Horizontal Pod Autoscaling (HPA) - new pods can't join mesh automatically
- Manual scaling (kubectl scale statefulset nexusd --replicas=5)
- Rolling updates with changing peer count

#### Proposed Solution

Use Kubernetes API to watch pod endpoints:

```bash
nexusd \
  --discovery k8s \
  --k8s-namespace nexusd \
  --k8s-label-selector app=nexusd \
  --k8s-port-name mesh
```

#### Implementation Details

**New CLI Flags:**

| Flag | Type | Default | Description |
|------|------|---------|-------------|
| `--discovery` | enum | multicast | Discovery method: multicast, k8s, dns, consul, static |
| `--k8s-namespace` | string | (current) | K8s namespace to watch for pods |
| `--k8s-label-selector` | string | app=nexusd | Label selector for peer pods |
| `--k8s-port-name` | string | mesh | Port name to use for connections |
| `--k8s-kubeconfig` | string | (in-cluster) | Path to kubeconfig file |
| `--k8s-sync-interval` | int (sec) | 30 | How often to resync endpoints |

**Dependencies:**

Add to `CMakeLists.txt`:
```cmake
FetchContent_Declare(
  cpp-k8s
  GIT_REPOSITORY https://github.com/kubernetes-client/cpp.git
  GIT_TAG        v0.1.0
)
FetchContent_MakeAvailable(cpp-k8s)
```

**Code Changes:**

```cpp
// src/discovery/k8s_discovery.hpp
#pragma once

#include <k8s/client.hpp>
#include <k8s/watch.hpp>
#include "discovery_agent.hpp"

namespace nexusd::discovery {

class KubernetesDiscoveryAgent : public DiscoveryAgent {
public:
    struct Config {
        std::string namespace_;
        std::string label_selector;
        std::string port_name;
        std::string kubeconfig_path;
        std::chrono::seconds sync_interval{30};
    };

    KubernetesDiscoveryAgent(
        const Config& config,
        PeerRegistry& registry,
        Logger& logger
    );

    void start() override;
    void stop() override;

private:
    void watchEndpoints();
    void handleEndpointEvent(const k8s::Event<k8s::Endpoints>& event);
    std::vector<PeerInfo> extractPeers(const k8s::Endpoints& endpoints);
    
    Config config_;
    PeerRegistry& registry_;
    Logger& logger_;
    
    std::unique_ptr<k8s::Client> k8s_client_;
    std::unique_ptr<k8s::Watch<k8s::Endpoints>> watcher_;
    std::atomic<bool> running_{false};
    std::thread watch_thread_;
};

} // namespace nexusd::discovery

// src/discovery/k8s_discovery.cpp
void KubernetesDiscoveryAgent::watchEndpoints() {
    k8s::WatchOptions options;
    options.label_selector = config_.label_selector;
    
    watcher_ = k8s_client_->watchEndpoints(
        config_.namespace_,
        [this](const k8s::Event<k8s::Endpoints>& event) {
            handleEndpointEvent(event);
        },
        options
    );
    
    logger_.info("Watching Endpoints in namespace={} with selector={}",
                config_.namespace_, config_.label_selector);
}

void KubernetesDiscoveryAgent::handleEndpointEvent(
    const k8s::Event<k8s::Endpoints>& event
) {
    switch (event.type) {
        case k8s::EventType::ADDED:
        case k8s::EventType::MODIFIED: {
            auto peers = extractPeers(event.object);
            for (const auto& peer : peers) {
                registry_.addOrUpdatePeer(peer);
                logger_.info("Discovered K8s peer: {} @ {}",
                           peer.instance_id, peer.address);
            }
            break;
        }
        case k8s::EventType::DELETED: {
            auto peers = extractPeers(event.object);
            for (const auto& peer : peers) {
                registry_.removePeer(peer.instance_id);
                logger_.info("Removed K8s peer: {}", peer.instance_id);
            }
            break;
        }
    }
}

std::vector<PeerInfo> KubernetesDiscoveryAgent::extractPeers(
    const k8s::Endpoints& endpoints
) {
    std::vector<PeerInfo> peers;
    
    for (const auto& subset : endpoints.subsets) {
        // Find the mesh port
        auto port_it = std::find_if(
            subset.ports.begin(),
            subset.ports.end(),
            [this](const k8s::EndpointPort& p) {
                return p.name == config_.port_name;
            }
        );
        
        if (port_it == subset.ports.end()) {
            logger_.warn("Port {} not found in endpoints", config_.port_name);
            continue;
        }
        
        int port = port_it->port;
        
        for (const auto& address : subset.addresses) {
            PeerInfo peer;
            peer.address = fmt::format("{}:{}", address.ip, port);
            
            // Use pod name as instance_id if available
            if (address.target_ref.has_value()) {
                peer.instance_id = address.target_ref->name;
            } else {
                peer.instance_id = address.ip; // Fallback
            }
            
            peers.push_back(peer);
        }
    }
    
    return peers;
}
```

**RBAC Requirements:**

```yaml
# k8s/rbac.yaml
apiVersion: v1
kind: ServiceAccount
metadata:
  name: nexusd
  namespace: nexusd
---
apiVersion: rbac.authorization.k8s.io/v1
kind: Role
metadata:
  name: nexusd-discovery
  namespace: nexusd
rules:
  - apiGroups: [""]
    resources: ["endpoints"]
    verbs: ["get", "list", "watch"]
---
apiVersion: rbac.authorization.k8s.io/v1
kind: RoleBinding
metadata:
  name: nexusd-discovery
  namespace: nexusd
subjects:
  - kind: ServiceAccount
    name: nexusd
roleRef:
  kind: Role
  name: nexusd-discovery
  apiGroup: rbac.authorization.k8s.io
```

**Testing Requirements:**

- [ ] Unit test: Parse Endpoints correctly
- [ ] Integration test: Connect to new pod when scaled up
- [ ] Integration test: Remove peer when pod deleted
- [ ] Integration test: Handle watch connection failures
- [ ] Chaos test: Kill and restart watch connection
- [ ] Load test: 100 pods joining simultaneously

**Success Criteria:**

- ✅ Mesh forms automatically without --peers flag
- ✅ Scaling up adds new peers within 10 seconds
- ✅ Scaling down removes peers within 10 seconds
- ✅ Works on EKS, GKE, and AKS
- ✅ Survives kube-apiserver restarts

---

### 1.3 DNS-Based Discovery

**Priority:** 🟡 Medium | **Effort:** Medium (3-4 weeks) | **Risk:** Low

#### Problem Statement

Not all environments have Kubernetes API access. Need a more portable discovery mechanism.

#### Proposed Solution

Use DNS SRV records for service discovery:

```bash
nexusd \
  --discovery dns \
  --dns-service _nexusd._tcp.nexusd-headless.nexusd.svc.cluster.local \
  --dns-refresh-interval 30
```

#### SRV Record Format

```
_nexusd._tcp.nexusd-headless.nexusd.svc.cluster.local.
  Priority Weight Port Target
  10       50     5671 nexusd-0.nexusd-headless.nexusd.svc.cluster.local.
  10       50     5671 nexusd-1.nexusd-headless.nexusd.svc.cluster.local.
  10       50     5671 nexusd-2.nexusd-headless.nexusd.svc.cluster.local.
```

#### Implementation Details

**Dependencies:**

```cmake
find_package(c-ares REQUIRED)
target_link_libraries(nexusd_core PRIVATE c-ares::cares)
```

**Code Changes:**

```cpp
// src/discovery/dns_discovery.hpp
#pragma once

#include <ares.h>
#include "discovery_agent.hpp"

namespace nexusd::discovery {

struct SRVRecord {
    std::string target;
    uint16_t port;
    uint16_t priority;
    uint16_t weight;
};

class DNSDiscoveryAgent : public DiscoveryAgent {
public:
    struct Config {
        std::string service_name;  // _nexusd._tcp.domain.com
        std::chrono::seconds refresh_interval{30};
        int timeout_ms = 5000;
    };

    DNSDiscoveryAgent(
        const Config& config,
        PeerRegistry& registry,
        Logger& logger
    );

    void start() override;
    void stop() override;

private:
    void queryLoop();
    std::vector<SRVRecord> querySRV(const std::string& service);
    void updatePeers(const std::vector<SRVRecord>& records);
    
    static void srvCallback(
        void* arg,
        int status,
        int timeouts,
        unsigned char* abuf,
        int alen
    );

    Config config_;
    PeerRegistry& registry_;
    Logger& logger_;
    
    ares_channel ares_channel_;
    std::atomic<bool> running_{false};
    std::thread query_thread_;
    std::set<std::string> known_peers_;
};

} // namespace nexusd::discovery
```

**Testing Requirements:**

- [ ] Unit test: Parse SRV records correctly
- [ ] Integration test: Discover peers via DNS
- [ ] Integration test: Handle DNS server failures
- [ ] Integration test: Update peers when DNS changes
- [ ] Performance test: DNS query overhead

**Success Criteria:**

- ✅ Works with Kubernetes headless services
- ✅ Works with Consul DNS
- ✅ Works with CoreDNS
- ✅ Handles DNS server unavailability gracefully

---

### 1.4 Consul/etcd Discovery

**Priority:** 🟢 Low | **Effort:** High (5-6 weeks) | **Risk:** Medium

#### Problem Statement

Organizations already using Consul or etcd for service discovery want native integration.

#### Proposed Solution

**Consul Integration:**

```bash
nexusd \
  --discovery consul \
  --consul-addr http://consul:8500 \
  --consul-service nexusd \
  --consul-datacenter dc1
```

**etcd Integration:**

```bash
nexusd \
  --discovery etcd \
  --etcd-endpoints http://etcd:2379 \
  --etcd-prefix /nexusd/peers
```

#### Implementation Details

**(Detailed implementation deferred to Phase 1.4 milestone document)**

---

## Phase 2: Observability

**Goal:** Provide production-grade visibility into NexusD operations for monitoring, debugging, and capacity planning.

**Business Value:** Operations teams can detect issues before users notice, debug problems efficiently, and plan infrastructure capacity.

### 2.1 Prometheus Metrics

**Priority:** 🔴 Critical | **Effort:** Medium (3-4 weeks) | **Risk:** Low

#### Problem Statement

No way to monitor NexusD health or performance. Questions like these can't be answered:
- How many messages per second are we processing?
- Which topics are most active?
- Are any mesh connections unhealthy?
- Is memory usage growing unbounded?

#### Proposed Solution

Expose metrics at `/metrics` endpoint in Prometheus format:

```bash
nexusd \
  --metrics-enabled \
  --metrics-port 9090 \
  --metrics-path /metrics \
  --metrics-update-interval 1000
```

#### Metrics Catalog

**Message Metrics:**

| Metric | Type | Labels | Description |
|--------|------|--------|-------------|
| `nexusd_messages_published_total` | Counter | `topic` | Total messages published |
| `nexusd_messages_delivered_total` | Counter | `topic`, `delivery_type` | Messages delivered (delivery_type: local, remote) |
| `nexusd_messages_forwarded_total` | Counter | `topic`, `target_peer` | Messages forwarded via mesh |
| `nexusd_messages_dropped_total` | Counter | `topic`, `reason` | Dropped messages (reason: queue_full, no_subscribers, ttl_expired) |
| `nexusd_message_size_bytes` | Histogram | `topic` | Message payload sizes (buckets: 1KB, 10KB, 100KB, 1MB, 10MB) |
| `nexusd_message_latency_seconds` | Histogram | `topic`, `delivery_type` | End-to-end delivery latency |

**Subscription Metrics:**

| Metric | Type | Labels | Description |
|--------|------|--------|-------------|
| `nexusd_active_subscriptions` | Gauge | `topic` | Current subscription count |
| `nexusd_active_topics` | Gauge | - | Number of topics with ≥1 subscriber |
| `nexusd_subscriber_queue_depth` | Gauge | `subscriber_id` | Messages pending delivery per subscriber |
| `nexusd_subscriber_queue_capacity` | Gauge | `subscriber_id` | Max queue size per subscriber |

**Mesh Metrics:**

| Metric | Type | Labels | Description |
|--------|------|--------|-------------|
| `nexusd_mesh_peers_connected` | Gauge | - | Currently connected peers |
| `nexusd_mesh_peers_total` | Gauge | `state` | All known peers (state: connected, disconnected, connecting) |
| `nexusd_mesh_connection_duration_seconds` | Gauge | `peer_id` | How long peer has been connected |
| `nexusd_mesh_rpc_requests_total` | Counter | `method`, `peer_id`, `status` | gRPC calls (method: PushMessage, GetNodeState; status: ok, error) |
| `nexusd_mesh_rpc_duration_seconds` | Histogram | `method`, `peer_id` | gRPC request latency |
| `nexusd_mesh_bytes_sent_total` | Counter | `peer_id` | Bytes sent to peer |
| `nexusd_mesh_bytes_received_total` | Counter | `peer_id` | Bytes received from peer |

**Discovery Metrics:**

| Metric | Type | Labels | Description |
|--------|------|--------|-------------|
| `nexusd_discovery_beacons_sent_total` | Counter | - | Multicast beacons transmitted |
| `nexusd_discovery_beacons_received_total` | Counter | `cluster_id` | Beacons received (filtered by cluster) |
| `nexusd_discovery_peers_discovered_total` | Counter | - | New peers found |
| `nexusd_discovery_sync_operations_total` | Counter | `trigger` | Gossip syncs (trigger: hash_mismatch, periodic, manual) |
| `nexusd_discovery_sync_duration_seconds` | Histogram | - | Gossip sync latency |

**Retained Message Metrics:**

| Metric | Type | Labels | Description |
|--------|------|--------|-------------|
| `nexusd_retained_messages` | Gauge | `topic` | Retained message count |
| `nexusd_retained_messages_bytes` | Gauge | - | Total retained message storage |

**System Metrics:**

| Metric | Type | Labels | Description |
|--------|------|--------|-------------|
| `nexusd_process_cpu_seconds_total` | Counter | - | CPU time consumed |
| `nexusd_process_memory_bytes` | Gauge | `type` | Memory usage (type: rss, vms, shared) |
| `nexusd_process_open_fds` | Gauge | - | Open file descriptors |
| `nexusd_process_threads` | Gauge | - | Thread count |
| `nexusd_uptime_seconds` | Gauge | - | Process uptime |

#### Implementation Details

**Dependencies:**

```cmake
FetchContent_Declare(
  prometheus-cpp
  GIT_REPOSITORY https://github.com/jupp0r/prometheus-cpp.git
  GIT_TAG        v1.1.0
)
FetchContent_MakeAvailable(prometheus-cpp)

target_link_libraries(nexusd_services PRIVATE 
  prometheus-cpp::pull
  prometheus-cpp::core
)
```

**Code Changes:**

```cpp
// src/observability/metrics_registry.hpp
#pragma once

#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>
#include <prometheus/registry.h>
#include <prometheus/exposer.h>

namespace nexusd::observability {

class MetricsRegistry {
public:
    static MetricsRegistry& instance();

    // Message metrics
    prometheus::Counter& messagesPublished(const std::string& topic);
    prometheus::Counter& messagesDelivered(const std::string& topic, 
                                          const std::string& delivery_type);
    prometheus::Histogram& messageSize(const std::string& topic);
    prometheus::Histogram& messageLatency(const std::string& topic,
                                         const std::string& delivery_type);

    // Subscription metrics
    prometheus::Gauge& activeSubscriptions(const std::string& topic);
    prometheus::Gauge& activeTopics();

    // Mesh metrics
    prometheus::Gauge& meshPeersConnected();
    prometheus::Counter& meshRPCRequests(const std::string& method,
                                        const std::string& peer_id,
                                        const std::string& status);
    
    void startExposer(const std::string& bind_address, int port);

private:
    MetricsRegistry();
    
    std::shared_ptr<prometheus::Registry> registry_;
    std::unique_ptr<prometheus::Exposer> exposer_;
    
    // Metric families
    prometheus::Family<prometheus::Counter>* messages_published_;
    prometheus::Family<prometheus::Counter>* messages_delivered_;
    prometheus::Family<prometheus::Histogram>* message_size_;
    // ... more families
};

} // namespace nexusd::observability

// Usage example in publish path:
void SidecarService::Publish(const PublishRequest& request) {
    auto& metrics = MetricsRegistry::instance();
    
    // Increment counter
    metrics.messagesPublished(request.topic()).Increment();
    
    // Record message size
    metrics.messageSize(request.topic()).Observe(request.payload().size());
    
    // ... publish logic
}
```

**Prometheus Configuration:**

```yaml
# prometheus.yml
scrape_configs:
  - job_name: 'nexusd'
    kubernetes_sd_configs:
      - role: pod
        namespaces:
          names:
            - nexusd
    relabel_configs:
      - source_labels: [__meta_kubernetes_pod_label_app]
        action: keep
        regex: nexusd
      - source_labels: [__meta_kubernetes_pod_ip]
        action: replace
        target_label: __address__
        replacement: '${1}:9090'
```

**Grafana Dashboard:**

Create `monitoring/grafana-dashboard.json` with panels:
- Message throughput (messages/sec by topic)
- Mesh connectivity (connected peers over time)
- Message latency (p50, p95, p99)
- Resource usage (CPU, memory)

**Testing Requirements:**

- [ ] Unit test: Metrics exported correctly
- [ ] Integration test: Scrape endpoint returns valid data
- [ ] Load test: Metrics overhead < 1% CPU
- [ ] Grafana test: All panels render without errors

**Success Criteria:**

- ✅ Prometheus scrapes `/metrics` successfully
- ✅ All critical metrics present
- ✅ Grafana dashboard shows real-time data
- ✅ Metrics cardinality < 10,000 time series

---

### 2.2 OpenTelemetry Distributed Tracing

**Priority:** 🟡 Medium | **Effort:** High (5-6 weeks) | **Risk:** Medium

#### Problem Statement

Message delivery involves multiple hops:
1. Client publishes to Node A
2. Node A routes to Node B (mesh forward)
3. Node B delivers to local subscriber

When messages are delayed or lost, it's impossible to pinpoint which hop is slow.

#### Proposed Solution

Instrument with OpenTelemetry for end-to-end tracing:

```bash
nexusd \
  --otel-enabled \
  --otel-endpoint http://jaeger:4317 \
  --otel-service-name nexusd \
  --otel-sample-rate 0.1
```

#### Trace Structure

```
Trace ID: 550e8400-e29b-41d4-a716-446655440000
  Span: client.publish (duration: 15ms)
    ├─ Span: server.route (duration: 2ms)
    │   └─ Attributes: topic=orders/new, local_subscribers=1, remote_peers=2
    ├─ Span: mesh.forward (duration: 8ms)
    │   ├─ Attributes: target_peer=nexusd-1, message_size=1024
    │   └─ Events: grpc_call_started, grpc_call_completed
    └─ Span: local.deliver (duration: 3ms)
        └─ Attributes: subscriber_id=order-processor
```

#### Implementation Details

**Dependencies:**

```cmake
FetchContent_Declare(
  opentelemetry-cpp
  GIT_REPOSITORY https://github.com/open-telemetry/opentelemetry-cpp.git
  GIT_TAG        v1.14.0
)
set(WITH_OTLP ON)
FetchContent_MakeAvailable(opentelemetry-cpp)
```

**Code Changes:**

```cpp
// src/observability/tracing.hpp
#pragma once

#include <opentelemetry/trace/provider.h>
#include <opentelemetry/sdk/trace/tracer_provider.h>
#include <opentelemetry/exporters/otlp/otlp_http_exporter.h>

namespace nexusd::observability {

class TracingManager {
public:
    static TracingManager& instance();
    
    void initialize(const std::string& endpoint,
                   const std::string& service_name,
                   double sample_rate);
    
    opentelemetry::trace::Tracer& tracer();
    
    // Helper to create span with common attributes
    auto startSpan(const std::string& operation,
                  const std::string& topic = "",
                  const std::string& peer_id = "");

private:
    std::shared_ptr<opentelemetry::trace::TracerProvider> provider_;
    std::shared_ptr<opentelemetry::trace::Tracer> tracer_;
};

// Usage in publish path:
void SidecarService::Publish(const PublishRequest& request) {
    auto& tracing = TracingManager::instance();
    auto span = tracing.startSpan("publish", request.topic());
    
    span->SetAttribute("message.size", request.payload().size());
    span->SetAttribute("message.topic", request.topic());
    
    try {
        // ... publish logic
        span->SetStatus(opentelemetry::trace::StatusCode::kOk);
    } catch (const std::exception& e) {
        span->SetStatus(opentelemetry::trace::StatusCode::kError);
        span->SetAttribute("error.message", e.what());
        throw;
    }
}
```

**Context Propagation:**

```protobuf
// proto/nexusd.proto
message Message {
  string topic = 1;
  bytes payload = 2;
  int64 timestamp = 3;
  
  // New field for trace context
  map<string, string> trace_context = 4;
}
```

**Testing Requirements:**

- [ ] Integration test: Traces appear in Jaeger
- [ ] Integration test: Context propagated across mesh
- [ ] Load test: Tracing overhead < 2% latency increase
- [ ] Chaos test: Missing trace context handled gracefully

**Success Criteria:**

- ✅ End-to-end traces visible in Jaeger/Zipkin
- ✅ Multi-hop message flows traced correctly
- ✅ Sampling rate configurable
- ✅ Performance impact acceptable

---

### 2.3 Structured Logging

**Priority:** 🟡 Medium | **Effort:** Low (1-2 weeks) | **Risk:** Low

#### Problem Statement

Current text logs are hard to parse:
```
[2024-01-15 10:30:45] [INFO] Peer connected: 550e8400... @ 10.0.0.5:5671
```

Log aggregation systems (ELK, Loki) work better with structured JSON.

#### Proposed Solution

```bash
nexusd --log-format json
```

**Output:**
```json
{
  "timestamp": "2024-01-15T10:30:45.123Z",
  "level": "INFO",
  "message": "Peer connected",
  "peer_id": "550e8400-e29b-41d4-a716-446655440000",
  "peer_address": "10.0.0.5:5671",
  "cluster_id": "production",
  "node_id": "nexusd-0"
}
```

#### Implementation Details

Use `spdlog` with JSON formatter:

```cpp
// src/utils/logger.cpp
void Logger::configure(const LogConfig& config) {
    if (config.format == LogFormat::JSON) {
        auto json_formatter = std::make_unique<JsonFormatter>();
        spdlog::set_formatter(std::move(json_formatter));
    }
}

class JsonFormatter : public spdlog::formatter {
public:
    void format(const spdlog::details::log_msg& msg, 
               spdlog::memory_buf_t& dest) override {
        nlohmann::json j;
        j["timestamp"] = formatTimestamp(msg.time);
        j["level"] = levelToString(msg.level);
        j["message"] = std::string(msg.payload.begin(), msg.payload.end());
        
        // Add context fields if present
        for (const auto& [key, value] : msg.context) {
            j[key] = value;
        }
        
        dest.append(j.dump());
        dest.push_back('\n');
    }
};
```

**Success Criteria:**

- ✅ JSON logs parseable by jq
- ✅ Fields indexed correctly in Elasticsearch
- ✅ Context fields propagate through call stack

---

### 2.4 Health Check Endpoints

**Priority:** 🔴 Critical | **Effort:** Low (1 week) | **Risk:** Low

#### Problem Statement

Kubernetes probes use TCP socket checks, which don't verify actual health:
- Process might be alive but mesh disconnected
- Process might be alive but gRPC server deadlocked

#### Proposed Solution

HTTP health endpoints:

```bash
nexusd --health-port 8080
```

**Endpoints:**

| Endpoint | Purpose | K8s Probe | Returns |
|----------|---------|-----------|---------|
| `/healthz` | Liveness | liveness | 200 if process alive |
| `/readyz` | Readiness | readiness | 200 if ready for traffic |
| `/livez` | Detailed | - | JSON with component status |

#### Implementation Details

```cpp
// src/observability/health_server.hpp
class HealthServer {
public:
    enum class ComponentStatus {
        HEALTHY,
        DEGRADED,
        UNHEALTHY
    };

    struct ComponentHealth {
        std::string name;
        ComponentStatus status;
        std::string message;
        std::chrono::system_clock::time_point last_check;
    };

    void registerCheck(const std::string& component,
                      std::function<ComponentHealth()> check_fn);
    
    void start(int port);

private:
    void handleHealthz(HttpRequest& req, HttpResponse& resp);
    void handleReadyz(HttpRequest& req, HttpResponse& resp);
    void handleLivez(HttpRequest& req, HttpResponse& resp);
    
    std::map<std::string, std::function<ComponentHealth()>> checks_;
};

// Register checks:
health_server.registerCheck("mesh", []() {
    auto peer_count = peer_registry.connectedPeerCount();
    if (peer_count == 0) {
        return ComponentHealth{
            "mesh",
            ComponentStatus::UNHEALTHY,
            "No peers connected",
            std::chrono::system_clock::now()
        };
    }
    return ComponentHealth{
        "mesh",
        ComponentStatus::HEALTHY,
        fmt::format("{} peers connected", peer_count),
        std::chrono::system_clock::now()
    };
});
```

**Kubernetes Integration:**

```yaml
# statefulset.yaml
livenessProbe:
  httpGet:
    path: /healthz
    port: 8080
  initialDelaySeconds: 10
  periodSeconds: 10

readinessProbe:
  httpGet:
    path: /readyz
    port: 8080
  initialDelaySeconds: 5
  periodSeconds: 5
```

**Success Criteria:**

- ✅ K8s probes work correctly
- ✅ Unhealthy pods removed from service
- ✅ Component-level health visible via /livez

---

## Phase 3-7 Summary

**(Detailed specifications for remaining phases continue...)**

Due to length constraints, I've provided comprehensive detail for Phases 1-2. The complete roadmap would continue with:

**Phase 3: Security** (mTLS, authentication, authorization, encryption)
**Phase 4: Resilience** (circuit breakers, backpressure, graceful shutdown)
**Phase 5: Performance** (batching, zero-copy, sharding)
**Phase 6: Operations** (Helm, operators, admin API)
**Phase 7: Advanced Features** (filtering, DLQ, multi-region)

Each phase follows the same structure:
- Problem statement
- Proposed solution
- Implementation details with code
- Testing requirements
- Success criteria

---

## Appendix A: Deployment Checklist

Before deploying to production, verify:

- [ ] All Phase 1 features implemented (discovery works in your environment)
- [ ] All Phase 2 features implemented (metrics, tracing, logging, health)
- [ ] All Phase 3 features implemented (mTLS, authentication)
- [ ] Runbooks documented for common failures
- [ ] Load tested at 2x expected traffic
- [ ] Chaos tested (kill pods, network partitions)
- [ ] Backup/restore procedures tested
- [ ] Monitoring dashboards created and reviewed
- [ ] Alerts configured with appropriate thresholds
- [ ] On-call rotation established

---

## Appendix B: Performance Targets

| Metric | Target | Measurement Method |
|--------|--------|-------------------|
| Message throughput | 100,000 msg/sec (single node) | Benchmark test |
| Message latency p99 | < 10ms (local delivery) | OpenTelemetry |
| Message latency p99 | < 50ms (mesh delivery) | OpenTelemetry |
| Mesh convergence time | < 30 seconds (10 nodes) | Integration test |
| CPU usage | < 20% (idle), < 80% (peak) | Prometheus |
| Memory usage | < 512MB (stable) | Prometheus |
| Connection overhead | < 1MB per peer | Profiling |

---

## Appendix C: Comparison with Alternatives

| Feature | NexusD (after roadmap) | NATS | Kafka | RabbitMQ |
|---------|------------------------|------|-------|----------|
| Brokerless | ✅ | ❌ | ❌ | ❌ |
| Peer discovery | ✅ Auto | ⚠️ Static | ⚠️ Static | ⚠️ Static |
| Horizontal scaling | ✅ Unlimited | ✅ Clusters | ⚠️ Partitions | ⚠️ Sharding |
| Message persistence | ✅ Optional | ✅ JetStream | ✅ Always | ✅ Always |
| Message ordering | ✅ Per-topic | ✅ | ✅ | ⚠️ Limited |
| Multi-region | ✅ Planned | ✅ Leaf nodes | ⚠️ MirrorMaker | ❌ |
| Observability | ✅ OTel + Prometheus | ✅ | ✅ | ✅ |
| Cloud-native | ✅ K8s-first | ✅ | ⚠️ Complex | ⚠️ Complex |

---

## Contributing to This Roadmap

See `CONTRIBUTING.md` for how to propose roadmap changes or claim features.

**Labels:**
- `roadmap/phase-1` through `roadmap/phase-7`
- `good-first-issue` - Beginner-friendly
- `help-wanted` - Need contributors
- `blocked` - Waiting on dependencies

**Feature Proposal Process:**

1. Open GitHub issue with template
2. Community discussion (2 weeks)
3. Design document if needed
4. Implementation PR
5. Review and merge
6. Update roadmap status
