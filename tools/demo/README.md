# NexusD Demo Cluster

Scripts for running a local NexusD cluster for testing and development.

## Quick Start

### Linux/macOS

```bash
# Start 3-node cluster
./start.sh

# Explore with CLI
nexusd-cli DISCOVER
nexusd-cli -n 1

# Stop cluster
./stop.sh

# Stop and clean data
./stop.sh --clean
```

### Windows

```batch
REM Start cluster
start.bat

REM Explore with CLI
nexusd-cli DISCOVER
nexusd-cli -n 1

REM Stop cluster
stop.bat

REM Stop and clean data
stop.bat --clean
```

## Configuration

Edit `cluster.conf` to customize:

| Setting | Default | Description |
|---------|---------|-------------|
| `CLUSTER_NAME` | demo-cluster | Name prefix for node IDs |
| `CLUSTER_NODE_COUNT` | 3 | Number of nodes to start |
| `BASE_APP_PORT` | 5672 | First node's gRPC port |
| `BASE_MESH_PORT` | 5680 | First node's mesh port |
| `MULTICAST_GROUP` | 239.255.77.77 | Discovery multicast address |
| `LOG_LEVEL` | info | Logging verbosity |
| `NEXUSD_BIN` | ../../build/nexusd | Path to daemon binary |

### Node Ports

With default settings and 3 nodes:

| Node | App Port | Mesh Port | Discovery Port |
|------|----------|-----------|----------------|
| 1 | 5672 | 5680 | 5690 |
| 2 | 5673 | 5681 | 5691 |
| 3 | 5674 | 5682 | 5692 |

## Directory Structure

After running:

```
tools/demo/
├── cluster.conf      # Configuration
├── start.sh          # Start script (Linux/macOS)
├── start.bat         # Start script (Windows)
├── stop.sh           # Stop script (Linux/macOS)
├── stop.bat          # Stop script (Windows)
├── README.md         # This file
├── .cluster.pids     # Running process IDs (auto-generated)
├── logs/             # Log files (auto-generated)
│   ├── node-1.log
│   ├── node-2.log
│   └── node-3.log
└── data/             # Node data (auto-generated)
    ├── node-1/
    ├── node-2/
    └── node-3/
```

## CLI Examples

After starting the cluster:

```bash
# Discover all instances
$ nexusd-cli DISCOVER
Discovering NexusD instances... found 3

#   ADDRESS              VERSION     STATUS    UPTIME
------------------------------------------------------
1   localhost:5672       1.0.0       healthy   5s
2   localhost:5673       1.0.0       healthy   4s
3   localhost:5674       1.0.0       healthy   3s

# Connect and explore
$ nexusd-cli -p 5672
localhost:5672> INFO
# Server
version:     1.0.0
instance_id: demo-cluster-node-1
uptime:      10s

localhost:5672> MESH PEERS
ID                  ADDRESS              STATUS      LATENCY
------------------------------------------------------------
demo-cluster-node-2 localhost:5681       healthy     1ms
demo-cluster-node-3 localhost:5682       healthy     1ms

localhost:5672> PUBSUB TOPICS
(empty list)

# Publish from one terminal
localhost:5672> PUBLISH test/topic "Hello World"
(integer) 0

# Subscribe from another
localhost:5673> SUBSCRIBE test/#
Subscribing to 1 pattern(s)...
1) message
   topic: test/topic
   data:  Hello World
```

## Troubleshooting

### Binary not found

Build NexusD first:

```bash
cd ../..
mkdir -p build && cd build
cmake ..
cmake --build .
```

### Port already in use

Change `BASE_APP_PORT` in `cluster.conf` or stop existing instances.

### Nodes not discovering each other

Check that:
1. Multicast is enabled on your network interface
2. Firewall allows UDP on the multicast port
3. All nodes use the same `MULTICAST_GROUP` and `MULTICAST_PORT`
