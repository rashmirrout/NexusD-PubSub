# Docker Deployment Guide

This guide provides step-by-step instructions for building and running NexusD in Docker containers.

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Building the Docker Image](#step-1-build-the-docker-image)
3. [Running a Single Node](#step-2-run-a-single-node)
4. [Running a Multi-Node Cluster](#step-3-run-a-multi-node-cluster)
5. [Testing the Cluster](#step-4-test-the-cluster)
6. [Viewing Logs](#step-5-view-logs)
7. [Stopping the Cluster](#step-6-stop-the-cluster)
8. [Troubleshooting](#troubleshooting)

---

## Prerequisites

- Docker 20.10+ installed
- Docker Compose v2.0+ (optional, for multi-node setup)
- 4GB+ RAM available for building (gRPC compilation is memory-intensive)

---

## Step 1: Build the Docker Image

Create a `Dockerfile` in the project root:

```dockerfile
# =============================================================================
# Stage 1: Build Environment
# =============================================================================
FROM ubuntu:22.04 AS builder

# What happens: Install build dependencies
# - cmake: Build system generator
# - g++: C++17 compiler
# - git: For fetching gRPC via FetchContent
# - libssl-dev: Required by gRPC for TLS support
RUN apt-get update && apt-get install -y \
    cmake \
    g++ \
    git \
    make \
    libssl-dev \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# What happens: Copy source code into container
# We copy everything needed for the build
COPY CMakeLists.txt ./
COPY src/ ./src/
COPY include/ ./include/
COPY proto/ ./proto/

# What happens: Configure and build NexusD
# - CMAKE_BUILD_TYPE=Release: Optimized binary, no debug symbols
# - NEXUSD_BUILD_TESTS=OFF: Skip tests to reduce build time
# - NEXUSD_FETCH_GRPC=ON: Download and build gRPC (takes ~10-15 min first time)
# - -j$(nproc): Use all CPU cores for parallel compilation
RUN mkdir build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release \
          -DNEXUSD_BUILD_TESTS=OFF \
          -DNEXUSD_FETCH_GRPC=ON \
          .. && \
    cmake --build . --config Release -j$(nproc)

# =============================================================================
# Stage 2: Runtime Environment
# =============================================================================
FROM ubuntu:22.04 AS runtime

# What happens: Install only runtime dependencies
# - libssl3: TLS library needed by gRPC at runtime
# - ca-certificates: For HTTPS connections if needed
RUN apt-get update && apt-get install -y \
    libssl3 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# What happens: Copy only the compiled binaries from builder stage
# This creates a minimal image (~50MB vs ~2GB for builder)
COPY --from=builder /build/build/bin/nexusd /app/
COPY --from=builder /build/build/lib/*.so* /app/lib/

# What happens: Set library path so nexusd can find shared libraries
ENV LD_LIBRARY_PATH=/app/lib

# What happens: Expose network ports
# - 5670/udp: Multicast discovery (peer beacon)
# - 5671/tcp: Mesh gRPC (inter-node communication)
# - 5672/tcp: App gRPC (sidecar API for applications)
EXPOSE 5670/udp 5671/tcp 5672/tcp

# What happens: Set default command with configurable arguments
# Users can override these via docker run or docker-compose
ENTRYPOINT ["/app/nexusd"]
CMD ["--cluster", "docker", "--log-level", "INFO"]
```

**Build the image:**

```bash
# What happens: Docker reads Dockerfile and builds in two stages
# Stage 1 compiles the code (~15 min first time due to gRPC)
# Stage 2 copies only binaries into a slim runtime image
docker build -t nexusd:latest .
```

**What happens internally:**

1. **Stage 1 (builder)**: Downloads Ubuntu 22.04, installs CMake/g++/git, copies source code, runs CMake configure + build. gRPC is fetched and compiled (this takes the longest).
2. **Stage 2 (runtime)**: Starts fresh Ubuntu 22.04, installs only libssl3, copies the compiled `nexusd` binary and `.so` libraries from Stage 1.
3. **Result**: A ~100MB image containing only what's needed to run NexusD.

---

## Step 2: Run a Single Node

```bash
# What happens: Start one NexusD container with:
# - --network host: Share host's network stack (required for UDP multicast)
# - -d: Run in background (detached mode)
# - Named "nexusd-single" for easy reference
docker run -d --name nexusd-single \
    --network host \
    nexusd:latest \
    --cluster test \
    --log-level DEBUG
```

**Why `--network host`?**

UDP multicast (239.255.42.1:5670) requires the container to share the host's network namespace. Docker's default bridge network doesn't forward multicast traffic between containers.

**Verify it's running:**

```bash
# What happens: Shows container status, uptime, and port mappings
docker ps | grep nexusd

# What happens: Stream live logs from the container
docker logs -f nexusd-single
```

---

## Step 3: Run a Multi-Node Cluster

Create a `docker-compose.yml` file:

```yaml
# =============================================================================
# NexusD 3-Node Cluster
# =============================================================================
# What happens: Defines 3 NexusD containers that form a mesh network
# Each node discovers others via UDP multicast on the shared network

version: '3.8'

services:
  # -------------------------------------------------------------------------
  # Node 1
  # -------------------------------------------------------------------------
  nexusd-1:
    image: nexusd:latest
    container_name: nexusd-1
    # What happens: All containers share a macvlan network for multicast support
    networks:
      nexusd-net:
        ipv4_address: 172.28.0.11
    # What happens: Configure this node's ports and cluster membership
    command:
      - --cluster
      - docker-cluster
      - --mesh-port
      - "5671"
      - --app-port
      - "5672"
      - --log-level
      - INFO
    # What happens: Expose app port to host for client connections
    ports:
      - "5672:5672"

  # -------------------------------------------------------------------------
  # Node 2
  # -------------------------------------------------------------------------
  nexusd-2:
    image: nexusd:latest
    container_name: nexusd-2
    networks:
      nexusd-net:
        ipv4_address: 172.28.0.12
    command:
      - --cluster
      - docker-cluster
      - --mesh-port
      - "5671"
      - --app-port
      - "5673"
      - --log-level
      - INFO
    ports:
      - "5673:5673"

  # -------------------------------------------------------------------------
  # Node 3
  # -------------------------------------------------------------------------
  nexusd-3:
    image: nexusd:latest
    container_name: nexusd-3
    networks:
      nexusd-net:
        ipv4_address: 172.28.0.13
    command:
      - --cluster
      - docker-cluster
      - --mesh-port
      - "5671"
      - --app-port
      - "5674"
      - --log-level
      - INFO
    ports:
      - "5674:5674"

# =============================================================================
# Network Configuration
# =============================================================================
networks:
  nexusd-net:
    driver: bridge
    # What happens: Enable multicast support on this network
    driver_opts:
      com.docker.network.driver.mtu: 1500
    ipam:
      config:
        - subnet: 172.28.0.0/16
```

**Alternative: Using Static Peer List (No Multicast)**

If multicast doesn't work in your Docker environment, use the `--peers` flag:

```yaml
services:
  nexusd-1:
    image: nexusd:latest
    command:
      - --cluster
      - docker-cluster
      - --peers
      - "172.28.0.12:5671,172.28.0.13:5671"
      - --mesh-port
      - "5671"
      - --app-port
      - "5672"
    # ... rest of config
```

**Start the cluster:**

```bash
# What happens: Docker Compose creates the network, then starts all 3 containers
# Containers boot in parallel, discover each other via multicast within seconds
docker-compose up -d

# What happens: Shows all running containers with their status
docker-compose ps
```

**Expected output:**

```
NAME        IMAGE          COMMAND                  STATUS          PORTS
nexusd-1    nexusd:latest  "/app/nexusd --clust…"   Up 10 seconds   0.0.0.0:5672->5672/tcp
nexusd-2    nexusd:latest  "/app/nexusd --clust…"   Up 10 seconds   0.0.0.0:5673->5673/tcp
nexusd-3    nexusd:latest  "/app/nexusd --clust…"   Up 10 seconds   0.0.0.0:5674->5674/tcp
```

---

## Step 4: Test the Cluster

**Check mesh connectivity:**

```bash
# What happens: Use nexusd-cli to discover running instances
# Should show all 3 nodes with their addresses
./build/bin/nexusd-cli DISCOVER

# What happens: Connect to node 1 and list its mesh peers
./build/bin/nexusd-cli -a localhost:5672 MESH PEERS
```

**Expected output:**

```
PEER_ID                               ADDRESS         STATE    LATENCY
550e8400-e29b-41d4-a716-446655440001  172.28.0.12:5671  CONNECTED  2ms
550e8400-e29b-41d4-a716-446655440002  172.28.0.13:5671  CONNECTED  1ms
```

**Test pub/sub across nodes:**

```bash
# Terminal 1: Subscribe on node 1
# What happens: Opens gRPC stream to node 1, waits for messages on "test/topic"
./build/bin/nexusd-cli -a localhost:5672 SUBSCRIBE test/topic

# Terminal 2: Publish from node 3
# What happens: Sends message to node 3, which forwards to node 1 via mesh
./build/bin/nexusd-cli -a localhost:5674 PUBLISH test/topic "Hello from node 3"
```

**What happens internally:**

1. Node 3 receives PUBLISH request via gRPC on port 5674
2. Node 3 checks local subscribers (none) and mesh routing table
3. Node 3 sees Node 1 has a subscriber for `test/topic`
4. Node 3 calls `PushMessage` RPC to Node 1's mesh port (5671)
5. Node 1 delivers message to local subscriber via gRPC stream

---

## Step 5: View Logs

```bash
# What happens: Stream logs from all 3 containers, prefixed by container name
docker-compose logs -f

# What happens: View only node 1's logs
docker-compose logs -f nexusd-1

# What happens: Show last 100 lines from all containers
docker-compose logs --tail=100
```

**Log format example:**

```
nexusd-1  | [2024-01-15 10:30:45.123] [INFO] Starting NexusD v1.0.0
nexusd-1  | [2024-01-15 10:30:45.124] [INFO] Cluster: docker-cluster
nexusd-1  | [2024-01-15 10:30:45.125] [INFO] Discovery: multicast 239.255.42.1:5670
nexusd-1  | [2024-01-15 10:30:45.130] [INFO] Mesh gRPC listening on 0.0.0.0:5671
nexusd-1  | [2024-01-15 10:30:45.131] [INFO] App gRPC listening on 0.0.0.0:5672
nexusd-1  | [2024-01-15 10:30:46.200] [INFO] Peer discovered: 550e8400... @ 172.28.0.12:5671
nexusd-1  | [2024-01-15 10:30:46.205] [INFO] Peer discovered: 550e8400... @ 172.28.0.13:5671
```

---

## Step 6: Stop the Cluster

```bash
# What happens: Sends SIGTERM to all containers, waits for graceful shutdown
# NexusD flushes pending messages and closes gRPC connections cleanly
docker-compose down

# What happens: Also removes the custom network
docker-compose down --volumes --remove-orphans
```

---

## Troubleshooting

### Nodes Don't Discover Each Other

**Symptom:** `MESH PEERS` shows empty list

**Cause:** UDP multicast not working in Docker network

**Solution 1:** Use host networking

```bash
docker run --network host nexusd:latest --cluster test
```

**Solution 2:** Use static peer list

```bash
docker run nexusd:latest \
    --cluster test \
    --peers "172.28.0.12:5671,172.28.0.13:5671"
```

### Build Fails with Out of Memory

**Symptom:** Compiler killed during gRPC build

**Cause:** gRPC compilation requires significant RAM

**Solution:** Limit parallel jobs

```dockerfile
# In Dockerfile, change:
RUN cmake --build . -j$(nproc)
# To:
RUN cmake --build . -j2
```

Or increase Docker's memory limit in Docker Desktop settings.

### Container Exits Immediately

**Symptom:** Container status shows "Exited (1)"

**Cause:** Missing shared libraries

**Solution:** Check LD_LIBRARY_PATH is set correctly

```bash
docker run -it nexusd:latest /bin/bash
ldd /app/nexusd  # Shows which libraries are missing
```

### Port Already in Use

**Symptom:** "Address already in use" error

**Solution:** Stop existing containers or change ports

```bash
docker-compose down
# Or use different ports:
docker run -p 5682:5672 nexusd:latest --app-port 5672
```

---

## Next Steps

- [Kubernetes Deployment](KUBERNETES.md) — Deploy NexusD on Kubernetes with StatefulSet
- [Architecture Overview](ARCHITECTURE.md) — Understanding NexusD internals
- [CLI Reference](../tools/cli/README.md) — Full command documentation
