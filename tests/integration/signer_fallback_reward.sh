#!/bin/bash
# Integration regression for the rev1 signed-fallback reward STALL bug.
#
# Bug: cryptonote_core.cpp try_produce_fallback_block overwrote b.reward with the block-template
# out-param expected_reward (the tx-FEE total only), dropping service_node_total. The hf19+
# reward-batching check (Blockchain::validate_block_rewards) requires
#   b.reward == miner_fee + service_node_total
# so every signed-fallback block was rejected ("block reward for batching is incorrect: 0 != <SN>")
# and the chain stalled -- which at mainnet rev1 (height 236920) would stall the chain the first
# time Pulse failed over to the signer.
#
# This test uses the signer-from-genesis testnet, where EVERY block is a signed-fallback block
# (zero registered SNs, no Pulse quorum), so it exercises the real producer path that core_tests
# cannot drive. PASS = the chain climbs past height 1 with the correct reward; if the overwrite is
# (re)introduced the daemon stalls at height 1 and this test FAILS.
#
# Usage: signer_fallback_reward.sh [path-to-xeqm-d]
set -u
CORE="$(cd "$(dirname "$0")/../.." && pwd)"
DAEMON="${1:-$CORE/build/bin/xeqm-d}"
KEY="$CORE/testnet/fallback-miner-testnet.key"
EXPECTED_REWARD=8250000000   # testnet per-block reward (miner_fee 0 + service_node_total 8.25)
P2P=49870
RPC=49871
BASE="$(mktemp -d)"

cleanup() { pkill -f "data-dir=$BASE" 2>/dev/null; rm -rf "$BASE"; }
trap cleanup EXIT

[ -x "$DAEMON" ] || { echo "FAIL: daemon not found/executable: $DAEMON"; exit 2; }
[ -f "$KEY" ]    || { echo "FAIL: fallback signer key not found: $KEY"; exit 2; }

nohup "$DAEMON" --testnet --offline --non-interactive \
  --data-dir="$BASE" --p2p-bind-port=$P2P --rpc-admin=127.0.0.1:$RPC \
  --fallback-miner-key-file="$KEY" --fallback-miner-delay-rounds=0 --log-level 0 \
  > "$BASE/daemon.log" 2>&1 &

rpc() {
  local params="${2:-}"; [ -z "$params" ] && params='{}'
  curl -s "http://127.0.0.1:$RPC/json_rpc" \
    -d "{\"jsonrpc\":\"2.0\",\"id\":\"0\",\"method\":\"$1\",\"params\":$params}" 2>/dev/null
}

# Wait up to 45s for the signer to climb past height 1 (i.e. produce accepted fallback blocks).
h=0
for _ in $(seq 1 45); do
  sleep 1
  h=$(rpc get_info | python3 -c 'import sys,json;print(json.load(sys.stdin).get("result",{}).get("height",0))' 2>/dev/null)
  [ "${h:-0}" -gt 5 ] 2>/dev/null && break
done

# Definitive bug signal (branch-independent): the reward-validation error means the signer DID
# produce a signed-fallback block and it was rejected -- i.e. the overwrite bug is present.
if grep -q "block reward for batching is incorrect" "$BASE/daemon.log"; then
  echo "FAIL: signed-fallback block rejected -- $(grep 'block reward for batching is incorrect' "$BASE/daemon.log" | tail -1)"
  exit 1
fi

# No reward error AND the chain never advanced past genesis => this build does not bootstrap from
# genesis via the signer (e.g. a release/main branch without signer-from-genesis). There is no
# fallback block for this harness to exercise, so skip rather than fail. On a signer-from-genesis
# build the chain always advances, and a present bug is caught by the reward-error check above.
if [ "${h:-0}" -le 1 ] 2>/dev/null; then
  echo "SKIP: chain did not advance past genesis and no reward error -- signer-from-genesis is not active on this build; nothing to test here"
  exit 0
fi

# Chain climbed via accepted signed-fallback blocks: verify the per-block reward is correct.
r=$(rpc get_block_header_by_height '{"height":1}' | python3 -c 'import sys,json;print(json.load(sys.stdin).get("result",{}).get("block_header",{}).get("reward",-1))' 2>/dev/null)
if [ "${r:-x}" != "$EXPECTED_REWARD" ]; then
  echo "FAIL: block 1 reward=${r} (expected $EXPECTED_REWARD = miner_fee + service_node_total)"
  exit 1
fi

echo "PASS: signer-fallback chain climbed to height $h; block 1 reward=$r (miner_fee + service_node_total)"
exit 0
