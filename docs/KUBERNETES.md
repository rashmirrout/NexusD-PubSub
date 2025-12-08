# Kubernetes Deployment Guide

This guide provides step-by-step instructions for deploying NexusD on Kubernetes using StatefulSet.

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Build and Push Image](#step-1-build-and-push-docker-image)
3. [Create Namespace](#step-2-create-namespace)
4. [Create ConfigMap](#step-3-create-configmap)
5. [Create Headless Service](#step-4-create-headless-service)
6. [Deploy StatefulSet](#step-5-deploy-statefulset)
7. [Create LoadBalancer Service](#step-6-create-loadbalancer-service)
8. [Verify Deployment](#step-7-verify-deployment)
9. [Test Pub/Sub](#step-8-test-pubsub)
10. [Clean Up](#step-9-clean-up)
11. [UDP Multicast Workarounds](#udp-multicast-workarounds)
12. [Future Enhancement: Built-in Retry](#future-enhancement-built-in-peer-retry-logic)

---

## Prerequisites

- Kubernetes cluster 1.21+ (minikube, kind, EKS, GKE, AKS)
- `kubectl` configured to access your cluster
- Docker registry access (Docker Hub, ECR, GCR, ACR)
- NexusD Docker image built (see [DOCKER.md](DOCKER.md))

---

## Step 1: Build and Push Docker Image

```bash
# What happens: Build the NexusD image locally
# See DOCKER.md for the complete Dockerfile
docker build -t nexusd:latest .

# What happens: Tag image for your registry
# Replace 'your-registry' with your actual registry URL
docker tag nexusd:latest your-registry/nexusd:v1.0.0

# What happens: Push image to registry so Kubernetes can pull it
# Registry must be accessible from your K8s cluster nodes
docker push your-registry/nexusd:v1.0.0
```

**Why push to a registry?**

Kubernetes nodes pull images from registries. Local Docker images aren't accessible to cluster nodes unless you're using minikube with `eval $(minikube docker-env)`.

---

## Step 2: Create Namespace

Create `namespace.yaml`:

```yaml
# =============================================================================
# Kubernetes Namespace
# =============================================================================
# What happens: Creates an isolated namespace for NexusD resources
# Benefits:
# - Resource isolation from other applications
# - Easier cleanup (delete namespace removes everything)
# - Can apply resource quotas per namespace

apiVersion: v1
kind: Namespace
metadata:
  name: nexusd
  labels:
    app: nexusd
    environment: production
```

Apply it:

```bash
# What happens: Kubernetes creates the namespace object
# All subsequent resources will be created in this namespace
kubectl apply -f namespace.yaml
```

---

## Step 3: Create ConfigMap

Create `configmap.yaml`:

```yaml
# =============================================================================
# NexusD Configuration
# =============================================================================
# What happens: Stores configuration values that pods can reference
# Separates config from pod spec, making it easy to update without redeploying

apiVersion: v1
kind: ConfigMap
metadata:
  name: nexusd-config
  namespace: nexusd
data:
  # Cluster identifier - nodes only discover peers with same cluster ID
  CLUSTER_ID: "k8s-production"
  
  # Log verbosity: TRACE, DEBUG, INFO, WARN, ERROR, FATAL
  LOG_LEVEL: "INFO"
  
  # Number of replicas (used by startup script to build peer list)
  REPLICA_COUNT: "3"
  
  # gRPC ports
  MESH_PORT: "5671"
  APP_PORT: "5672"
```

Apply it:

```bash
# What happens: ConfigMap is stored in etcd (K8s datastore)
# Pods reference these values via envFrom or env.valueFrom
kubectl apply -f configmap.yaml
```

---

## Step 4: Create Headless Service

Create `headless-service.yaml`:

```yaml
# =============================================================================
# Headless Service for StatefulSet DNS
# =============================================================================
# What happens: Creates DNS records for each pod without load balancing
# 
# Regular Service: nexusd.nexusd.svc.cluster.local -> random pod IP
# Headless Service: nexusd-0.nexusd-headless.nexusd.svc.cluster.local -> pod-0 IP
#                   nexusd-1.nexusd-headless.nexusd.svc.cluster.local -> pod-1 IP
#
# Why headless (clusterIP: None)?
# - Each NexusD node needs to connect to SPECIFIC peers, not random ones
# - StatefulSet pods get stable network identities via headless service
# - Enables static peer list: --peers nexusd-0:5671,nexusd-1:5671,nexusd-2:5671

apiVersion: v1
kind: Service
metadata:
  name: nexusd-headless
  namespace: nexusd
  labels:
    app: nexusd
spec:
  clusterIP: None  # This makes it "headless"
  selector:
    app: nexusd
  ports:
    # Mesh port for inter-node gRPC communication
    - name: mesh
      port: 5671
      targetPort: 5671
    # App port for sidecar API
    - name: app
      port: 5672
      targetPort: 5672
```

Apply it:

```bash
# What happens: Kubernetes creates the headless service
# DNS entries are automatically created for pods matching the selector
kubectl apply -f headless-service.yaml
```

**Verify DNS works:**

```bash
# What happens: Run a debug pod to test DNS resolution
kubectl run -it --rm debug --image=busybox --namespace=nexusd -- nslookup nexusd-headless
```

---

## Step 5: Deploy StatefulSet

Create `statefulset.yaml`:

```yaml
# =============================================================================
# NexusD StatefulSet
# =============================================================================
# What happens: Creates 3 ordered, stable pods with predictable names
# 
# Why StatefulSet instead of Deployment?
# - Stable network identity: nexusd-0, nexusd-1, nexusd-2 (not random hashes)
# - Ordered startup: nexusd-0 starts first, then nexusd-1, etc.
# - Stable DNS: Each pod gets its own DNS record via headless service
# - Required for building static peer lists with known addresses

apiVersion: apps/v1
kind: StatefulSet
metadata:
  name: nexusd
  namespace: nexusd
spec:
  serviceName: nexusd-headless  # Links to headless service for DNS
  replicas: 3
  
  # What happens: Pods are created one at a time in order (0, 1, 2)
  # Each pod must be Running before the next one starts
  podManagementPolicy: OrderedReady
  
  selector:
    matchLabels:
      app: nexusd
      
  template:
    metadata:
      labels:
        app: nexusd
    spec:
      # -----------------------------------------------------------------------
      # Init Container: Wait for Peer DNS
      # -----------------------------------------------------------------------
      # What happens: Runs BEFORE the main container starts
      # Waits until at least nexusd-0's DNS is resolvable
      # 
      # Why is this needed?
      # - StatefulSet pods start sequentially, but DNS propagation takes time
      # - If nexusd-1 starts before nexusd-0's DNS is ready, --peers will fail
      # - Init container blocks until DNS is available, ensuring reliable startup
      #
      # Without init container:
      #   nexusd-1 starts → tries to connect to nexusd-0:5671 → DNS lookup fails
      #   → daemon may crash or run without mesh connectivity
      #
      # With init container:
      #   init waits → DNS ready → nexusd-1 starts → connects successfully
      
      initContainers:
        - name: wait-for-dns
          image: busybox:1.36
          command:
            - sh
            - -c
            - |
              echo "Waiting for headless service DNS to be ready..."
              # Loop until nexusd-0's DNS record exists
              until nslookup nexusd-0.nexusd-headless.nexusd.svc.cluster.local; do
                echo "DNS not ready, sleeping 2s..."
                sleep 2
              done
              echo "DNS is ready, starting main container"
          
      # -----------------------------------------------------------------------
      # Main Container
      # -----------------------------------------------------------------------
      containers:
        - name: nexusd
          image: your-registry/nexusd:v1.0.0  # Replace with your image
          imagePullPolicy: Always
          
          # What happens: Expose ports for mesh and app communication
          ports:
            - name: mesh
              containerPort: 5671
              protocol: TCP
            - name: app
              containerPort: 5672
              protocol: TCP
          
          # What happens: Load config values from ConfigMap as environment variables
          envFrom:
            - configMapRef:
                name: nexusd-config
          
          # What happens: Additional environment variables for peer list generation
          env:
            # Pod's own hostname (nexusd-0, nexusd-1, etc.)
            - name: POD_NAME
              valueFrom:
                fieldRef:
                  fieldPath: metadata.name
            # Full DNS suffix for peer addresses
            - name: SERVICE_NAME
              value: "nexusd-headless.nexusd.svc.cluster.local"
          
          # What happens: Build startup command with static peer list
          # Uses shell to dynamically construct --peers argument
          command:
            - /bin/sh
            - -c
            - |
              # Build peer list from StatefulSet pod names
              # Format: nexusd-0.nexusd-headless.nexusd.svc.cluster.local:5671,...
              PEERS=""
              for i in $(seq 0 $((REPLICA_COUNT - 1))); do
                PEER="nexusd-${i}.${SERVICE_NAME}:${MESH_PORT}"
                if [ -z "$PEERS" ]; then
                  PEERS="$PEER"
                else
                  PEERS="${PEERS},${PEER}"
                fi
              done
              
              echo "Starting NexusD with peers: $PEERS"
              
              exec /app/nexusd \
                --cluster "$CLUSTER_ID" \
                --peers "$PEERS" \
                --mesh-port "$MESH_PORT" \
                --app-port "$APP_PORT" \
                --log-level "$LOG_LEVEL"
          
          # What happens: Kubernetes checks if container is alive
          # If 3 consecutive checks fail, container is restarted
          livenessProbe:
            tcpSocket:
              port: 5671
            initialDelaySeconds: 10
            periodSeconds: 10
            failureThreshold: 3
          
          # What happens: Kubernetes checks if container is ready for traffic
          # Pod only receives traffic when readiness probe passes
          readinessProbe:
            tcpSocket:
              port: 5672
            initialDelaySeconds: 5
            periodSeconds: 5
            failureThreshold: 3
          
          # Resource limits
          resources:
            requests:
              memory: "128Mi"
              cpu: "100m"
            limits:
              memory: "512Mi"
              cpu: "500m"
```

Apply it:

```bash
# What happens: Kubernetes creates the StatefulSet controller
# Controller creates pods one at a time: nexusd-0 → nexusd-1 → nexusd-2
# Each pod runs init container first, then main container
kubectl apply -f statefulset.yaml
```

**Watch the rollout:**

```bash
# What happens: Shows pod creation progress in real-time
kubectl get pods -n nexusd -w

# Expected output over ~1 minute:
# nexusd-0   0/1     Init:0/1   0          5s
# nexusd-0   0/1     PodInitializing   0   8s
# nexusd-0   1/1     Running           0   10s
# nexusd-1   0/1     Pending           0   0s
# nexusd-1   0/1     Init:0/1          0   2s
# nexusd-1   1/1     Running           0   12s
# nexusd-2   0/1     Init:0/1          0   2s
# nexusd-2   1/1     Running           0   12s
```

---

## Step 6: Create LoadBalancer Service

Create `loadbalancer-service.yaml`:

```yaml
# =============================================================================
# LoadBalancer Service for External Access
# =============================================================================
# What happens: Exposes NexusD to external clients
# Cloud providers (EKS/GKE/AKS) provision an actual load balancer
# For local K8s (minikube), use NodePort or port-forward instead

apiVersion: v1
kind: Service
metadata:
  name: nexusd-lb
  namespace: nexusd
spec:
  type: LoadBalancer
  selector:
    app: nexusd
  ports:
    - name: app
      port: 5672
      targetPort: 5672
```

Apply it:

```bash
kubectl apply -f loadbalancer-service.yaml

# What happens: Get the external IP (may take 1-2 minutes on cloud)
kubectl get svc nexusd-lb -n nexusd -w
```

**For local development (minikube/kind):**

```bash
# What happens: Forward local port to a specific pod
kubectl port-forward -n nexusd svc/nexusd-headless 5672:5672

# Or forward to specific pod:
kubectl port-forward -n nexusd nexusd-0 5672:5672
```

---

## Step 7: Verify Deployment

**Check all resources:**

```bash
# What happens: List all NexusD resources in the namespace
kubectl get all -n nexusd
```

**Expected output:**

```
NAME           READY   STATUS    RESTARTS   AGE
pod/nexusd-0   1/1     Running   0          2m
pod/nexusd-1   1/1     Running   0          1m30s
pod/nexusd-2   1/1     Running   0          1m

NAME                      TYPE           CLUSTER-IP      EXTERNAL-IP     PORT(S)
service/nexusd-headless   ClusterIP      None            <none>          5671/TCP,5672/TCP
service/nexusd-lb         LoadBalancer   10.100.45.123   34.123.45.678   5672:31234/TCP

NAME                      READY   AGE
statefulset.apps/nexusd   3/3     2m
```

**Check mesh connectivity:**

```bash
# What happens: Exec into nexusd-0 and check its peer connections
kubectl exec -it nexusd-0 -n nexusd -- /app/nexusd-cli MESH PEERS

# Expected: Should show nexusd-1 and nexusd-2 as connected peers
```

**Check logs:**

```bash
# What happens: Stream logs from all pods
kubectl logs -f -l app=nexusd -n nexusd --prefix

# Or specific pod:
kubectl logs -f nexusd-0 -n nexusd
```

---

## Step 8: Test Pub/Sub

**Subscribe on one pod:**

```bash
# Terminal 1: Subscribe via nexusd-0
kubectl exec -it nexusd-0 -n nexusd -- \
    /app/nexusd-cli SUBSCRIBE test/topic
```

**Publish from another pod:**

```bash
# Terminal 2: Publish via nexusd-2
kubectl exec -it nexusd-2 -n nexusd -- \
    /app/nexusd-cli PUBLISH test/topic "Hello from Kubernetes!"
```

**What happens internally:**

1. Subscriber on nexusd-0 registers interest in `test/topic`
2. nexusd-0's topic state hash changes → gossip sync triggers
3. nexusd-1 and nexusd-2 learn that nexusd-0 subscribes to `test/topic`
4. Publisher on nexusd-2 sends message
5. nexusd-2 checks routing table → sees nexusd-0 is interested
6. nexusd-2 calls `PushMessage` RPC to nexusd-0.nexusd-headless:5671
7. nexusd-0 delivers message to local subscriber

---

## Step 9: Clean Up

```bash
# What happens: Delete all resources in the namespace
kubectl delete namespace nexusd

# What happens: This removes:
# - All 3 StatefulSet pods
# - Both services (headless and loadbalancer)
# - ConfigMap
# - The namespace itself
```

---

## UDP Multicast Workarounds

NexusD uses UDP multicast (239.255.42.1:5670) for automatic peer discovery. **Kubernetes does not support UDP multicast** in standard overlay networks (Calico, Flannel, Cilium). Here are the workarounds:

### Comparison Table

| Option | Approach | Pros | Cons |
|--------|----------|------|------|
| **A** | Host Networking | Multicast works natively, zero config | Breaks pod isolation, port conflicts, security concerns |
| **B** | Weave Net CNI | Overlay with multicast support | Requires CNI plugin change, operational overhead |
| **C** | Static Peer List | Portable, works everywhere | Requires known replica count, uses DNS |

### Option A: Host Networking

```yaml
# In StatefulSet pod spec:
spec:
  hostNetwork: true  # Share host's network namespace
  dnsPolicy: ClusterFirstWithHostNet
```

**Why it works:**
- Pod uses the node's network interface directly
- Multicast traffic goes through physical NIC, not overlay
- Discovery beacons reach all pods on same L2 network

**Why NOT recommended:**
- **Security:** Pod can see all host network traffic
- **Port conflicts:** Multiple pods on same node can't use same port
- **Isolation:** Breaks Kubernetes network model
- **Cloud limitations:** May not work in cloud provider networks

### Option B: Weave Net CNI

Install Weave Net instead of default CNI:

```bash
kubectl apply -f https://github.com/weaveworks/weave/releases/download/v2.8.1/weave-daemonset-k8s.yaml
```

**Why it works:**
- Weave Net implements a virtual L2 network across nodes
- Supports multicast at the overlay level
- NexusD discovery works without code changes

**Why NOT recommended for most cases:**
- **Operational complexity:** Replacing CNI is invasive
- **Vendor lock-in:** Tied to Weave's release cycle
- **Managed K8s:** EKS/GKE/AKS don't easily allow CNI changes

### Option C: Static Peer List (Recommended)

Use `--peers` flag with Kubernetes DNS (as shown in StatefulSet above).

**Why recommended:**

1. **Portable:** Works on any Kubernetes cluster (EKS, GKE, AKS, on-prem)
2. **No CNI changes:** Uses standard Kubernetes networking
3. **Pod isolation preserved:** Normal network policies still work
4. **DNS-based:** Leverages K8s built-in service discovery
5. **Already supported:** `--peers` flag exists in NexusD (no code changes)

**How it works:**

```
StatefulSet creates pods with stable names:
  nexusd-0, nexusd-1, nexusd-2

Headless service creates DNS records:
  nexusd-0.nexusd-headless.nexusd.svc.cluster.local → 10.244.1.5
  nexusd-1.nexusd-headless.nexusd.svc.cluster.local → 10.244.2.8
  nexusd-2.nexusd-headless.nexusd.svc.cluster.local → 10.244.3.2

NexusD startup:
  --peers nexusd-0...:5671,nexusd-1...:5671,nexusd-2...:5671

Result: Mesh connectivity without multicast
```

**Trade-off:**

- Must know replica count at deploy time (via ConfigMap)
- Scaling requires ConfigMap update + rolling restart

---

## Future Enhancement: Built-in Peer Retry Logic

### The Problem Today

The current `--peers` flag requires all peer DNS names to be resolvable at startup. In Kubernetes:

1. StatefulSet starts `nexusd-0`
2. `nexusd-1` starts, tries to connect to `nexusd-2:5671`
3. `nexusd-2` doesn't exist yet → DNS lookup fails
4. `nexusd-1` may crash or start with incomplete peer list

**Current solution:** Init container waits for DNS (as shown above).

### Proposed Enhancement

Add built-in retry logic to eliminate init container dependency:

```bash
nexusd \
  --peers "nexusd-0:5671,nexusd-1:5671,nexusd-2:5671" \
  --peer-retry-count 10 \
  --peer-retry-interval 2000
```

| Flag | Default | Description |
|------|---------|-------------|
| `--peer-retry-count` | 10 | Number of DNS resolution attempts before giving up |
| `--peer-retry-interval` | 2000 | Milliseconds between retry attempts |

### Why This Would Help

1. **Eliminates init container complexity**
   - No need for busybox sidecar image
   - No shell scripts in pod spec
   - Cleaner, more declarative YAML

2. **Graceful degradation**
   - NexusD starts immediately with available peers
   - Background thread keeps trying unreachable peers
   - Full mesh forms as pods become ready

3. **Dynamic scaling**
   - Scale from 3 to 5 replicas
   - Existing pods don't need restart
   - New pods join mesh automatically

4. **Self-healing**
   - If peer temporarily goes down (rolling update), auto-reconnect
   - No manual intervention needed

### Why Init Container Is Still Needed Today

- Current `--peers` implementation does **one-shot DNS resolution**
- No built-in retry/backoff mechanism
- Failed lookup → incomplete mesh or startup failure
- Init container guarantees DNS is ready before `nexusd` starts

### Implementation Location

If you want to contribute this feature:

```
src/core/peer_connector.cpp  ← Add retry loop here
src/core/peer_connector.hpp  ← Add retry config members
src/main.cpp                 ← Parse new CLI flags
```

**Pseudocode:**

```cpp
void PeerConnector::connectToPeers(const std::vector<std::string>& peers) {
    for (const auto& peer : peers) {
        int attempts = 0;
        while (attempts < retry_count_) {
            auto addr = resolveDNS(peer);
            if (addr.has_value()) {
                connectTo(addr.value());
                break;
            }
            ++attempts;
            std::this_thread::sleep_for(
                std::chrono::milliseconds(retry_interval_ms_));
            logger_.warn("Peer {} DNS failed, retrying ({}/{})", 
                         peer, attempts, retry_count_);
        }
    }
}
```

---

## Next Steps

- [Docker Deployment](DOCKER.md) — Local container testing
- [Architecture Overview](ARCHITECTURE.md) — System design and protocols
- [CLI Reference](../tools/cli/README.md) — Command-line tool documentation
