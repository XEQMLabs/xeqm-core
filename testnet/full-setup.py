#!/usr/bin/env python3
"""Full testnet setup: create wallet, mine funds, register 12 SNs."""
import json, subprocess, time, urllib.request, sys, os

SEED_RPC    = "http://127.0.0.1:49001/json_rpc"
WALLET_RPC  = "http://127.0.0.1:18183/json_rpc"
WALLET_DIR  = "/home/svshearer/xeqm-testnet/wallet"
WALLET_NAME = "testnet-gov"
WALLET_PASS = "xeqm-testnet"
XEQM_RPC   = "/home/svshearer/xeqm-core/build/bin/xeqm-rpc"
STAKING_AMT = 100_000_000_000   # 100 XEQM
COIN        = 1_000_000_000

def rpc(url, method, params=None):
    body = json.dumps({"jsonrpc": "2.0", "method": method,
                       "params": params or {}, "id": "0"}).encode()
    req = urllib.request.Request(url, data=body,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=15) as r:
        d = json.load(r)
    if "error" in d:
        raise RuntimeError(f"{method}: {d['error']['message']}")
    return d.get("result", {})

def wait_for_rpc(url, label, timeout=60):
    for _ in range(timeout // 2):
        try:
            rpc(url, "get_info" if "49001" in url else "get_version")
            print(f"  {label} ready")
            return True
        except Exception:
            time.sleep(2)
    print(f"  {label} timed out!")
    return False

# ── 1. Ensure wallet-rpc is running ──────────────────────────────────────────
pid_file = "/home/svshearer/xeqm-testnet/pids/wallet-rpc.pid"
try:
    pid = int(open(pid_file).read().strip())
    os.kill(pid, 0)
    print(f"wallet-rpc already running (pid {pid})")
except Exception:
    log = open("/home/svshearer/xeqm-testnet/logs/wallet-rpc.log", "a")
    proc = subprocess.Popen([
        XEQM_RPC, "--testnet",
        "--wallet-dir", WALLET_DIR,
        "--rpc-bind-port", "18183",
        "--daemon-address", "127.0.0.1:49001",
        "--disable-rpc-login", "--log-level", "0",
    ], stdout=log, stderr=log,
       preexec_fn=os.setsid)
    open(pid_file, "w").write(str(proc.pid))
    print(f"wallet-rpc started (pid {proc.pid}) — polling...")
    wait_for_rpc(WALLET_RPC, "wallet-rpc")

# ── 2. Open testnet-gov wallet (governance payments accumulate here) ──────────
# testnet-gov == GOVERNANCE_WALLET_ADDRESS in testnet.h, so it receives all
# governance payments (4.125 XEQM/block) and can fund SN registrations.
print("Opening testnet-gov wallet...")
rpc(WALLET_RPC, "open_wallet", {"filename": WALLET_NAME, "password": WALLET_PASS})
print("  wallet opened")

# ── 3. Get wallet address + rescan (wallet may have stale state from old chain) ──
try:
    rpc(WALLET_RPC, "rescan_blockchain", {})
    print("  wallet rescanned from genesis")
except Exception as e:
    print(f"  rescan skipped ({e})")
addr_info = rpc(WALLET_RPC, "get_address", {"account_index": 0})
WALLET_ADDR = addr_info["address"]
print(f"Wallet address: {WALLET_ADDR}")
open(os.path.join(WALLET_DIR, "address.txt"), "w").write(WALLET_ADDR)

# ── 4. Restart seed with mining to wallet address ─────────────────────────────
DATA_DIR = "/home/svshearer/xeqm-testnet/data"
PID_DIR  = "/home/svshearer/xeqm-testnet/pids"
LOG_DIR  = "/home/svshearer/xeqm-testnet/logs"
xeqmd    = "/home/svshearer/xeqm-core/build/bin/xeqm-d"

def start_daemon(name, args):
    pid_file = f"{PID_DIR}/{name}.pid"
    try:
        pid = int(open(pid_file).read().strip())
        os.kill(pid, 0)
        print(f"  [{name}] already running (pid {pid})")
        return
    except Exception:
        pass
    os.makedirs(f"{DATA_DIR}/{name}", exist_ok=True)
    log = open(f"{LOG_DIR}/{name}.log", "a")
    proc = subprocess.Popen(
        [xeqmd, "--testnet", "--non-interactive",
         f"--data-dir={DATA_DIR}/{name}"] + args,
        stdout=log, stderr=log,
        preexec_fn=os.setsid)
    open(pid_file, "w").write(str(proc.pid))
    print(f"  [{name}] started pid {proc.pid}")

# Kill old seed (may have been started without --start-mining)
seed_pid_file = f"{PID_DIR}/seed.pid"
try:
    old_pid = int(open(seed_pid_file).read().strip())
    os.kill(old_pid, 15)
    time.sleep(2)
    os.remove(seed_pid_file)
except Exception:
    pass

# Option A: the seed mines fallback blocks and signs them with the governance spend key.
# Get this key once from the testnet-gov wallet:
#   curl -s http://127.0.0.1:18183/json_rpc -d '{"jsonrpc":"2.0","id":"0","method":"query_key","params":{"key_type":"spend_key"}}'
FALLBACK_MINER_KEY = "0c999a88be252215b48735272e5cf3dd86d54f850bb0bc8bfe9aea0de7549405"

print("Starting seed (mining) + 12 SNs...")
start_daemon("seed", [
    "--p2p-bind-ip=0.0.0.0", "--p2p-bind-port=49000",
    "--rpc-admin=127.0.0.1:49001",
    "--start-mining", WALLET_ADDR, "--mining-threads", "2",
    "--fallback-miner-key", FALLBACK_MINER_KEY,
])

for i in range(1, 13):
    p2p  = 49000 + i * 100
    rpc_ = p2p + 1
    qnet = p2p + 2
    start_daemon(f"snode{i}", [
        "--service-node", "--dev-allow-local-ips",
        "--p2p-bind-ip=0.0.0.0", f"--p2p-bind-port={p2p}",
        f"--rpc-admin=127.0.0.1:{rpc_}",
        f"--quorumnet-port={qnet}",
        "--service-node-public-ip=127.0.0.1",
        "--seed-node=127.0.0.1:49000",
        "--add-priority-node=127.0.0.1:49000",
    ])

print(f"All 13 daemons launched. Waiting for seed RPC...")
wait_for_rpc(SEED_RPC, "seed")

# ── 5. Wait for HF19 and enough balance ──────────────────────────────────────
print("Waiting for HF19 (block 9) and 1200 XEQM unlocked (up to 30 min)...")
for i in range(360):
    time.sleep(5)
    try:
        info = rpc(SEED_RPC, "get_info")
        hf = info.get("hard_fork", 0)
        height = info.get("height", 0)
        rpc(WALLET_RPC, "refresh", {})
        bal = rpc(WALLET_RPC, "get_balance", {"account_index": 0})
        unlocked = bal["unlocked_balance"] // COIN
        needed = 12 * STAKING_AMT // COIN
        print(f"  height={height} hf={hf} unlocked={unlocked} XEQM (need {needed})")
        if hf >= 19 and unlocked >= needed:
            print(f"  Ready to register!")
            break
    except Exception as e:
        print(f"  [{i}] polling... ({e})")
else:
    print("Timed out waiting for funds/HF19")
    sys.exit(1)

# ── 6. Register 12 SNs ────────────────────────────────────────────────────────
print(f"\n=== Registering 12 SNs (100 XEQM each, 10% fee) ===")
registered = 0
for i in range(1, 13):
    snode_rpc = f"http://127.0.0.1:{49100 + (i-1)*100 + 1}/json_rpc"
    print(f"  snode{i:02d} -> ", end="", flush=True)
    try:
        resp = rpc(snode_rpc, "get_service_node_registration_cmd", {
            "operator_cut": "10",
            "contributor_addresses": [WALLET_ADDR],
            "contributor_amounts": [STAKING_AMT],
            "staking_requirement": STAKING_AMT,
        })
        reg_cmd = resp.get("registration_cmd", "")
        if not reg_cmd:
            print("empty reg_cmd"); continue
        relay = rpc(WALLET_RPC, "register_service_node",
                    {"register_service_node_str": reg_cmd})
        print(relay.get("tx_hash", "no-txid"))
        registered += 1
    except Exception as e:
        print(f"ERROR: {e}")
    time.sleep(1)

print(f"\nRegistered: {registered} / 12")
print("HF22 fires at block 350 — monitor with watch-testnet.sh")
