#!/bin/bash
#
# NexusD Demo Cluster - Stop Script
# Gracefully stops all cluster nodes
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

# Load configuration for timeout
SHUTDOWN_TIMEOUT=5
if [[ -f "$CONFIG_FILE" ]]; then
    source "$CONFIG_FILE"
fi
SHUTDOWN_TIMEOUT="${SHUTDOWN_TIMEOUT:-5}"

echo -e "${BLUE}╔══════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║          NexusD Demo Cluster - Stopping                  ║${NC}"
echo -e "${BLUE}╚══════════════════════════════════════════════════════════╝${NC}"
echo ""

# Check if PID file exists
if [[ ! -f "$PID_FILE" ]]; then
    echo -e "${YELLOW}No cluster PID file found. Cluster may not be running.${NC}"
    echo ""
    
    # Try to find any nexusd processes anyway
    NEXUSD_PIDS=$(pgrep -f "nexusd.*--id.*demo-cluster" 2>/dev/null || true)
    if [[ -n "$NEXUSD_PIDS" ]]; then
        echo -e "Found orphaned NexusD processes: ${NEXUSD_PIDS}"
        echo -n "Kill them? [y/N] "
        read -r REPLY
        if [[ "$REPLY" =~ ^[Yy]$ ]]; then
            echo "$NEXUSD_PIDS" | xargs kill -TERM 2>/dev/null || true
            echo -e "${GREEN}Sent SIGTERM to orphaned processes.${NC}"
        fi
    fi
    exit 0
fi

# Read PIDs from file
PIDS=()
while read -r pid; do
    PIDS+=("$pid")
done < "$PID_FILE"

echo -e "  Found ${#PIDS[@]} node(s) to stop"
echo ""

# Send SIGTERM to all processes
STOPPED=0
for pid in "${PIDS[@]}"; do
    if kill -0 "$pid" 2>/dev/null; then
        echo -e "  Stopping node (PID: ${pid})..."
        kill -TERM "$pid" 2>/dev/null || true
        ((STOPPED++))
    else
        echo -e "  ${YELLOW}Node (PID: ${pid}) already stopped${NC}"
    fi
done

# Wait for graceful shutdown
if [[ $STOPPED -gt 0 ]]; then
    echo ""
    echo -e "  Waiting for graceful shutdown (timeout: ${SHUTDOWN_TIMEOUT}s)..."
    
    WAIT_COUNT=0
    while [[ $WAIT_COUNT -lt $SHUTDOWN_TIMEOUT ]]; do
        STILL_RUNNING=0
        for pid in "${PIDS[@]}"; do
            if kill -0 "$pid" 2>/dev/null; then
                ((STILL_RUNNING++))
            fi
        done
        
        if [[ $STILL_RUNNING -eq 0 ]]; then
            break
        fi
        
        sleep 1
        ((WAIT_COUNT++))
        echo -e "    ${STILL_RUNNING} node(s) still running..."
    done
    
    # Force kill any remaining processes
    FORCE_KILLED=0
    for pid in "${PIDS[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            echo -e "  ${YELLOW}Force killing node (PID: ${pid})...${NC}"
            kill -KILL "$pid" 2>/dev/null || true
            ((FORCE_KILLED++))
        fi
    done
    
    if [[ $FORCE_KILLED -gt 0 ]]; then
        echo -e "  ${YELLOW}${FORCE_KILLED} node(s) required force kill${NC}"
    fi
fi

# Remove PID file
rm -f "$PID_FILE"

echo ""
echo -e "${BLUE}══════════════════════════════════════════════════════════${NC}"
echo -e "${GREEN}✓ Cluster stopped successfully!${NC}"
echo ""

# Optionally clean up data
if [[ "$1" == "--clean" ]]; then
    echo -e "  Cleaning up data and logs..."
    rm -rf "${SCRIPT_DIR}/data" "${SCRIPT_DIR}/logs"
    echo -e "${GREEN}✓ Data and logs cleaned${NC}"
    echo ""
fi
