#!/bin/bash
#
# NexusD Demo Cluster - Start Script
# Starts a local cluster of NexusD instances for testing
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONFIG_FILE="${SCRIPT_DIR}/cluster.conf"
PID_FILE="${SCRIPT_DIR}/.cluster.pids"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Load configuration
if [[ ! -f "$CONFIG_FILE" ]]; then
    echo -e "${RED}Error: Configuration file not found: ${CONFIG_FILE}${NC}"
    exit 1
fi

source "$CONFIG_FILE"

# Validate configuration
CLUSTER_NAME="${CLUSTER_NAME:-demo-cluster}"
CLUSTER_NODE_COUNT="${CLUSTER_NODE_COUNT:-3}"
BASE_APP_PORT="${BASE_APP_PORT:-5672}"
BASE_MESH_PORT="${BASE_MESH_PORT:-5680}"
BASE_DISCOVERY_PORT="${BASE_DISCOVERY_PORT:-5690}"
NEXUSD_BIN="${NEXUSD_BIN:-../../build/nexusd}"
STARTUP_DELAY="${STARTUP_DELAY:-1}"
LOG_DIR="${LOG_DIR:-./logs}"
DATA_DIR="${DATA_DIR:-./data}"

# Resolve binary path
NEXUSD_PATH="${SCRIPT_DIR}/${NEXUSD_BIN}"
if [[ ! -x "$NEXUSD_PATH" ]]; then
    if [[ -x "$NEXUSD_BIN" ]]; then
        NEXUSD_PATH="$NEXUSD_BIN"
    else
        echo -e "${RED}Error: NexusD binary not found at: ${NEXUSD_PATH}${NC}"
        echo -e "${YELLOW}Please build NexusD first:${NC}"
        echo "  cd ../../build && cmake .. && cmake --build ."
        exit 1
    fi
fi

# Create directories
mkdir -p "${SCRIPT_DIR}/${LOG_DIR}" "${SCRIPT_DIR}/${DATA_DIR}"

# Check if cluster is already running
if [[ -f "$PID_FILE" ]]; then
    RUNNING_PIDS=""
    while read -r pid; do
        if kill -0 "$pid" 2>/dev/null; then
            RUNNING_PIDS="$RUNNING_PIDS $pid"
        fi
    done < "$PID_FILE"
    
    if [[ -n "$RUNNING_PIDS" ]]; then
        echo -e "${YELLOW}Warning: Cluster appears to be already running (PIDs:${RUNNING_PIDS})${NC}"
        echo -e "Run ${BLUE}./stop.sh${NC} first to stop the existing cluster."
        exit 1
    fi
    rm -f "$PID_FILE"
fi

echo -e "${BLUE}╔══════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║          NexusD Demo Cluster - Starting                  ║${NC}"
echo -e "${BLUE}╚══════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "  Cluster Name:  ${GREEN}${CLUSTER_NAME}${NC}"
echo -e "  Node Count:    ${GREEN}${CLUSTER_NODE_COUNT}${NC}"
echo -e "  Base App Port: ${GREEN}${BASE_APP_PORT}${NC}"
echo ""

# Build peer list for mesh
PEER_LIST=""
for ((i=1; i<=CLUSTER_NODE_COUNT; i++)); do
    MESH_PORT=$((BASE_MESH_PORT + i - 1))
    if [[ -n "$PEER_LIST" ]]; then
        PEER_LIST="${PEER_LIST},"
    fi
    PEER_LIST="${PEER_LIST}localhost:${MESH_PORT}"
done

# Start nodes
PIDS=()
for ((i=1; i<=CLUSTER_NODE_COUNT; i++)); do
    APP_PORT=$((BASE_APP_PORT + i - 1))
    MESH_PORT=$((BASE_MESH_PORT + i - 1))
    DISCOVERY_PORT=$((BASE_DISCOVERY_PORT + i - 1))
    NODE_ID="${CLUSTER_NAME}-node-${i}"
    NODE_DATA_DIR="${SCRIPT_DIR}/${DATA_DIR}/node-${i}"
    NODE_LOG_FILE="${SCRIPT_DIR}/${LOG_DIR}/node-${i}.log"
    
    mkdir -p "$NODE_DATA_DIR"
    
    echo -e "  Starting node ${i}..."
    echo -e "    ID:        ${NODE_ID}"
    echo -e "    App Port:  ${APP_PORT}"
    echo -e "    Mesh Port: ${MESH_PORT}"
    
    # Start the daemon
    "$NEXUSD_PATH" \
        --id "$NODE_ID" \
        --app-port "$APP_PORT" \
        --mesh-port "$MESH_PORT" \
        --discovery-port "$DISCOVERY_PORT" \
        --multicast-group "${MULTICAST_GROUP:-239.255.77.77}" \
        --multicast-port "${MULTICAST_PORT:-5670}" \
        --peers "$PEER_LIST" \
        --data-dir "$NODE_DATA_DIR" \
        --log-level "${LOG_LEVEL:-info}" \
        > "$NODE_LOG_FILE" 2>&1 &
    
    PID=$!
    PIDS+=("$PID")
    echo "$PID" >> "$PID_FILE"
    
    echo -e "    PID:       ${GREEN}${PID}${NC}"
    echo ""
    
    sleep "$STARTUP_DELAY"
done

# Wait for nodes to initialize
echo -e "  Waiting for nodes to initialize..."
sleep 2

# Verify nodes are running
RUNNING=0
FAILED=0
for ((i=0; i<${#PIDS[@]}; i++)); do
    if kill -0 "${PIDS[$i]}" 2>/dev/null; then
        ((RUNNING++))
    else
        ((FAILED++))
        echo -e "  ${RED}Node $((i+1)) failed to start (PID: ${PIDS[$i]})${NC}"
    fi
done

echo ""
echo -e "${BLUE}══════════════════════════════════════════════════════════${NC}"

if [[ $FAILED -gt 0 ]]; then
    echo -e "${YELLOW}Warning: ${FAILED} node(s) failed to start${NC}"
    echo -e "Check logs in: ${SCRIPT_DIR}/${LOG_DIR}"
else
    echo -e "${GREEN}✓ All ${RUNNING} nodes started successfully!${NC}"
fi

echo ""
echo -e "  Logs: ${SCRIPT_DIR}/${LOG_DIR}"
echo -e "  PIDs: ${PIDS[*]}"
echo ""
echo -e "${BLUE}══════════════════════════════════════════════════════════${NC}"
echo ""
echo -e "Use the CLI to explore the cluster:"
echo ""
echo -e "  ${GREEN}nexusd-cli DISCOVER${NC}"
echo -e "      Discover all running instances"
echo ""
echo -e "  ${GREEN}nexusd-cli -n 1${NC}"
echo -e "      Connect to first discovered instance"
echo ""
echo -e "  ${GREEN}nexusd-cli -p ${BASE_APP_PORT} INFO${NC}"
echo -e "      Get info from node 1"
echo ""
echo -e "  ${GREEN}nexusd-cli -p ${BASE_APP_PORT} MESH PEERS${NC}"
echo -e "      List mesh peers from node 1"
echo ""
echo -e "To stop the cluster:"
echo -e "  ${YELLOW}./stop.sh${NC}"
echo ""
