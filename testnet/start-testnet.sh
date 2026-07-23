#!/bin/bash
# Private XEQM testnet launcher — 1 seed + 12 SNs on Fernando
# 5s blocks; HF22 fires at block 350 (~20.75 min from genesis)
# Usage: ./start-testnet.sh [start|stop|status|logs]
set -euo pipefail

XEQMD=/home/svshearer/xeqm-core/build/bin/xeqm-d
BASE_DIR=/home/svshearer/xeqm-testnet/data
LOG_DIR=/home/svshearer/xeqm-testnet/logs
PID_DIR=/home/svshearer/xeqm-testnet/pids
FERNANDO_IP=$(curl -s --connect-timeout 3 https://api.ipify.org 2>/dev/null || hostname -I | awk '{print $1}')

mkdir -p "$BASE_DIR" "$LOG_DIR" "$PID_DIR"

# Ports: seed=49000, snode1=49100..49102, snode2=49200..49202, ..., snode12=50200..50202
SEED_P2P=49000; SEED_RPC=49001

start_daemon() {
    local name=$1; shift
    local pidfile="$PID_DIR/$name.pid"
    if [ -f "$pidfile" ] && kill -0 "$(cat "$pidfile")" 2>/dev/null; then
        echo "[$name] already running (pid $(cat "$pidfile"))"
        return
    fi
    mkdir -p "$BASE_DIR/$name"
    echo -n "[$name] starting... "
    "$XEQMD" --testnet --non-interactive --data-dir="$BASE_DIR/$name" "$@" \
        >> "$LOG_DIR/$name.log" 2>&1 &
    echo $! > "$pidfile"
    echo "pid $!"
}

stop_all() {
    for pidfile in "$PID_DIR"/*.pid; do
        [ -f "$pidfile" ] || continue
        pid=$(cat "$pidfile")
        name=$(basename "$pidfile" .pid)
        if kill -0 "$pid" 2>/dev/null; then
            echo "Stopping $name (pid $pid)..."
            kill "$pid"
        fi
        rm -f "$pidfile"
    done
    echo "All testnet daemons stopped."
}

status_all() {
    echo "=== Testnet daemon status ==="
    for pidfile in "$PID_DIR"/*.pid; do
        [ -f "$pidfile" ] || continue
        pid=$(cat "$pidfile")
        name=$(basename "$pidfile" .pid)
        if kill -0 "$pid" 2>/dev/null; then
            echo "  $name: RUNNING (pid $pid)"
        else
            echo "  $name: DEAD (stale pid $pid)"
        fi
    done
}

start_all() {
    echo "Starting private testnet on $FERNANDO_IP"
    echo "  5s blocks | HF16/pulse at block 240 | HF22 at block 350"
    echo ""

    # Seed node (non-SN) — authorized fallback miner.
    # MINING_ADDR: wallet address to mine to (must be the governance wallet for Option A).
    # FALLBACK_MINER_KEY: governance wallet spend secret key (hex); required for Option A so
    #   fallback blocks carry a valid governance signature and are accepted by all peers.
    local mining_args=()
    if [ -n "${MINING_ADDR:-}" ]; then
        mining_args=(--start-mining "$MINING_ADDR" --mining-threads 2)
        if [ -n "${FALLBACK_MINER_KEY:-}" ]; then
            mining_args+=(--fallback-miner-key "$FALLBACK_MINER_KEY")
        fi
    fi
    start_daemon "seed" \
        --p2p-bind-ip=0.0.0.0 \
        --p2p-bind-port=$SEED_P2P \
        --rpc-admin=127.0.0.1:$SEED_RPC \
        "${mining_args[@]}"

    # 12 service nodes
    for i in $(seq 1 12); do
        local p2p=$((49000 + i * 100))
        local rpc=$((p2p + 1))
        local qnet=$((p2p + 2))
        start_daemon "snode$i" \
            --service-node \
            --p2p-bind-ip=0.0.0.0 \
            --p2p-bind-port=$p2p \
            --rpc-admin=127.0.0.1:$rpc \
            --quorumnet-port=$qnet \
            --service-node-public-ip="$FERNANDO_IP" \
            --seed-node=127.0.0.1:$SEED_P2P \
            --add-priority-node=127.0.0.1:$SEED_P2P
    done

    echo ""
    echo "Seed RPC: http://127.0.0.1:$SEED_RPC"
    echo "SN RPCs:  http://127.0.0.1:49101 .. http://127.0.0.1:50201"
    echo "Logs:     $LOG_DIR/"
    echo ""
    echo "Next step: run ./setup-wallet.sh to create testnet wallet and register SNs."
}

case "${1:-start}" in
    start)  start_all ;;
    stop)   stop_all ;;
    status) status_all ;;
    logs)   tail -f "$LOG_DIR/seed.log" ;;
    *)      echo "Usage: $0 [start|stop|status|logs]" ;;
esac
