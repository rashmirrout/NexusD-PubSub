# NexusD CLI

Command-line interface for debugging and managing NexusD distributed pub/sub daemon instances.

## Features

- **Case-insensitive commands**: `DISCOVER`, `discover`, and `Discover` all work
- **Multi-instance discovery**: Find all NexusD instances on the network via UDP multicast
- **Redis-inspired interface**: Familiar command syntax for Redis users
- **JSON output**: Machine-readable output with `--json` flag
- **Tab completion**: Auto-complete commands and subcommands (uppercase)

## Building

```bash
cd tools/cli
mkdir build && cd build
cmake ..
cmake --build .
```

With system gRPC:
```bash
cmake -DCLI_USE_SYSTEM_GRPC=ON ..
```

## Usage

### Interactive Mode

```bash
# Start CLI (auto-connects to localhost:5672)
nexusd-cli

# Connect to specific port
nexusd-cli -p 5673

# Discover and connect to first instance
nexusd-cli -n 1

# Don't auto-connect
nexusd-cli --no-connect
```

### Single Command Mode

```bash
# Discover instances
nexusd-cli DISCOVER

# Get server info
nexusd-cli INFO

# Publish a message
nexusd-cli PUBLISH sensors/temp '{"value": 23.5}'

# JSON output
nexusd-cli --json INFO
```

## Commands

Commands are **case-insensitive**. Type `HELP` for available commands.

### Connection Commands

| Command | Description |
|---------|-------------|
| `DISCOVER` | Find NexusD instances on the network |
| `CONNECT <host:port>` | Connect to a specific instance |
| `CONNECT -n <index>` | Connect by discovery index |
| `DISCONNECT` | Disconnect from current instance |
| `PING` | Ping the connected server |

### Information Commands

| Command | Description |
|---------|-------------|
| `INFO` | Get server information |
| `INFO SERVER` | Server details (version, uptime) |
| `INFO STATS` | Statistics (topics, subscribers) |
| `INFO MEMORY` | Memory usage |
| `INFO CONFIG` | Configuration values |

### Pub/Sub Commands

| Command | Description |
|---------|-------------|
| `PUBSUB TOPICS` | List all topics |
| `PUBSUB NUMSUB <topic>` | Get subscriber count for topic |
| `PUBSUB RETAINED` | List topics with retained messages |
| `PUBLISH <topic> <msg>` | Publish a message |
| `SUBSCRIBE <pattern>` | Subscribe to topics |
| `MONITOR` | Monitor all messages in real-time |

### Mesh Commands

| Command | Description |
|---------|-------------|
| `MESH PEERS` | List connected mesh peers |
| `MESH STATUS` | Overall mesh health status |
| `MESH TOPOLOGY` | Visual mesh topology |

### Client Commands

| Command | Description |
|---------|-------------|
| `CLIENT LIST` | List connected clients |
| `CLIENT INFO <id>` | Get client details |

### Debug Commands

| Command | Description |
|---------|-------------|
| `DEBUG OBJECT` | Get debug info dump |
| `DEBUG SLEEP <ms>` | Sleep for testing |
| `DEBUG LOG` | Get/set log level |

### Utility Commands

| Command | Description |
|---------|-------------|
| `HELP` | Show help |
| `HELP <command>` | Help for specific command |
| `CLEAR` | Clear terminal |
| `QUIT` | Exit CLI |

## Command Aliases

| Alias | Command |
|-------|---------|
| `Q`, `EXIT` | `QUIT` |
| `?`, `H` | `HELP` |
| `DISC`, `LS` | `DISCOVER` |
| `PUB` | `PUBLISH` |
| `SUB` | `SUBSCRIBE` |
| `MON` | `MONITOR` |

## Examples

### Discover and Connect

```
(not connected)> DISCOVER
Discovering NexusD instances... found 3

#   ADDRESS              VERSION     STATUS    UPTIME    TOPICS  SUBS    PEERS
----------------------------------------------------------------------------------
1   192.168.1.10:5672    1.0.0       healthy   2h        15      42      2
2   192.168.1.11:5672    1.0.0       healthy   1h        12      38      2
3   192.168.1.12:5673    1.0.0       healthy   30m       8       20      2

To connect: CONNECT -n <#> or CONNECT <address>

(not connected)> CONNECT -n 1
OK: Connected to 192.168.1.10:5672
192.168.1.10:5672>
```

### Inspect Topics

```
192.168.1.10:5672> pubsub topics
TOPIC                                   SUBS        MESSAGES    RETAINED
--------------------------------------------------------------------------
sensors/temperature                     5           1234        yes
sensors/humidity                        3           892         no
config/settings                         10          15          yes
events/alerts                           8           45          no
```

### Monitor Messages

```
192.168.1.10:5672> monitor
Entering monitor mode. Press Ctrl+C to stop.
Topic filter: #

14:23:45.123 [sensors/temperature] "{"value": 23.5, "unit": "celsius"}"
14:23:45.456 [sensors/humidity] "{"value": 45, "unit": "%"}"
14:23:46.789 [events/alerts] "{"type": "warning", "message": "High temp"}"
^C
Monitor ended.
```

### JSON Output

```bash
$ nexusd-cli --json DISCOVER
[{"index":1,"address":"192.168.1.10:5672","version":"1.0.0","status":"healthy","uptime":7200,"topics":15,"subscribers":42,"peers":2}]

$ nexusd-cli --json INFO SERVER
{"server":{"version":"1.0.0","instance_id":"abc123","uptime_seconds":7200}}
```

## Options

| Option | Description |
|--------|-------------|
| `-h, --host <host>` | Connect to host (default: localhost) |
| `-p, --port <port>` | Connect to port (default: 5672) |
| `-n <index>` | Connect by discovery index |
| `--json` | Output in JSON format |
| `--no-connect` | Don't auto-connect on startup |
| `--timeout <ms>` | Discovery timeout (default: 2000) |
| `--help` | Show help |
| `--version` | Show version |
