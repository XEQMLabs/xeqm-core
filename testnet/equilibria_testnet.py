import subprocess
import time
import json
import requests
import argparse
import os
from pathlib import Path
import threading

class NetworkConfig:
    """Network configuration constants"""
    def __init__(self):
        self.genesis_address = "XEQTN3HrcXx7oWPEyWVzyw1V1GQFkXkPzJ9g7LyuHm5D6xbPFrAE8MyK7ZiVBp11ic72YQZwo6UzF2Rc5EWbnEHT99VbHLUx18"
        self.genesis_spend_key = "e0e3bfa8113406541ad8765bf1dddbf5151da1ad6f7586af8686bc5e5e15470b"
        self.genesis_view_key = "f49c400d21ef3f12854e3377d467ff63ba2d6013fa85d465f3c807b716b1c60b"
        self.wallet_rpc_port = 18084
        self.daemon_rpc_port = 18081
        self.staking_requirement = 15000000000000  # 15,000 XEQ in atomic units
        self.difficulty = 750
        self.unlock_window = 30  # Blocks needed for coin unlock
        self.hf16_height = 50    # HF16 activation height

class DockerManager:
    """Handles Docker container operations"""

    def __init__(self, config):
        self.config = config

    def run_command(self, cmd):
        """Run docker command with error handling"""
        try:
            result = subprocess.run(cmd, capture_output=True, text=True, check=False)

            if result.returncode != 0:
                print(f"❌ Docker command failed:")
                print(f"Command: {' '.join(cmd)}")
                print(f"Exit code: {result.returncode}")
                print(f"STDERR: {result.stderr}")
                self._cleanup_failed_container(cmd)
                return None

            if result.stdout:
                print(f"[DOCKER] {result.stdout.strip()}")
            return result

        except Exception as e:
            print(f"❌ Docker exception: {e}")
            return None

    def _cleanup_failed_container(self, cmd):
        """Clean up failed container"""
        if "--name" in cmd:
            name_idx = cmd.index("--name") + 1
            if name_idx < len(cmd):
                container_name = cmd[name_idx]
                subprocess.run(["docker", "rm", "-f", container_name], capture_output=True)

    def cleanup_all_containers(self, container_names):
        """Force remove all containers"""
        for container in container_names:
            subprocess.run(["docker", "kill", container], capture_output=True)
            subprocess.run(["docker", "rm", "-f", container], capture_output=True)
        print(f"🧹 Cleaned up {len(container_names)} containers")

class RPCClient:
    """Handles RPC communication with daemon and wallet"""

    def call(self, port, method, params=None):
        """Make RPC call"""
        if params is None:
            params = {}

        payload = {
            "jsonrpc": "2.0",
            "id": "0",
            "method": method,
            "params": params
        }

        try:
            response = requests.post(
                f"http://127.0.0.1:{port}/json_rpc",
                json=payload,
                timeout=30
            )
            return response.json()
        except Exception as e:
            print(f"RPC call failed: {method} on port {port} - {e}")
            return None

    def wait_for_service(self, port, timeout=60):
        """Wait for service to be ready"""
        start_time = time.time()
        while time.time() - start_time < timeout:
            try:
                response = requests.post(
                    f"http://127.0.0.1:{port}/json_rpc",
                    json={"jsonrpc": "2.0", "id": "0", "method": "get_info"},
                    timeout=5
                )
                if response.status_code == 200:
                    return True
            except:
                pass
            time.sleep(2)
        return False

class WalletManager:
    """Handles wallet operations"""

    def __init__(self, rpc_client, config):
        self.rpc = rpc_client
        self.config = config

    def setup_genesis_wallet(self):
        """Create and setup genesis wallet"""
        print("📝 Setting up genesis wallet...")

        result = self.rpc.call(self.config.wallet_rpc_port, "generate_from_keys", {
            "filename": "genesis",
            "password": "dummy",
            "address": self.config.genesis_address,
            "spendkey": self.config.genesis_spend_key,
            "viewkey": self.config.genesis_view_key,
            "restore_height": 0
        })

        if not result or "error" in result:
            print(f'Failed to generate genesis wallet: {result}')
            return False

        print("🔍 Rescanning blockchain...")
        self.rpc.call(self.config.wallet_rpc_port, "rescan_blockchain")
        time.sleep(5)
        self.refresh()
        return True

    def refresh(self):
        """Refresh wallet state"""
        self.rpc.call(self.config.wallet_rpc_port, "refresh")
        time.sleep(2)

    def get_balance(self):
        """Get wallet balance details"""
        result = self.rpc.call(self.config.wallet_rpc_port, "get_balance")
        if result and "result" in result:
            return result["result"]
        return {"balance": 0, "unlocked_balance": 0, "blocks_to_unlock": 0}

    def wait_for_unlocked_balance(self, required_amount):
        """Wait until sufficient unlocked balance is available"""
        print(f"⏳ Waiting for {required_amount:,} unlocked balance...")

        while True:
            self.refresh()
            balance_info = self.get_balance()
            unlocked = balance_info.get("unlocked_balance", 0)

            if unlocked >= required_amount:
                print(f"✅ Sufficient unlocked balance: {unlocked:,}")
                return True

            blocks_to_unlock = balance_info.get("blocks_to_unlock", 0)
            print(f"💰 Unlocked: {unlocked:,}, need: {required_amount:,}, blocks to unlock: {blocks_to_unlock}")
            time.sleep(5)

class ServiceNodeRegistrar:
    """Handles service node registration with robust funding and unlocking logic"""

    def __init__(self, rpc_client, wallet_manager, config):
        self.rpc = rpc_client
        self.wallet = wallet_manager
        self.config = config

    def register_node(self, node_id):
        """Orchestrates the full registration flow for a single node"""
        print(f"\n🚀 Starting automation for Service Node {node_id:02d}...")

        # 1. Create the SN Wallet
        sn_wallet_name = f"sn{node_id:02d}"
        sn_address = self._create_sn_wallet(sn_wallet_name)
        if not sn_address: return False

        # 2. Fund the SN Wallet from Genesis
        print(f"💰 Funding {sn_wallet_name}...")
        if not self._fund_wallet(sn_address, self.config.staking_requirement):
            return False

        # 3. Switch back to SN Wallet and WAIT for unlock
        # We need to be in the SN wallet to check its balance
        self.rpc.call(self.config.wallet_rpc_port, "open_wallet", {"filename": sn_wallet_name, "password": "dummy"})

        print(f"⏳ Waiting for funds to unlock in {sn_wallet_name} (approx 10 blocks)...")
        if not self._wait_for_unlock(self.config.staking_requirement):
            return False

        # 4. Get the Registration Command from the Daemon (not wallet)
        # Calculate SN RPC port based on your network logic (18091, 18093, etc)
        sn_daemon_rpc_port = 18091 + (node_id-1) * 2

        reg_cmd = self._get_registration_cmd(sn_daemon_rpc_port, sn_address)
        if not reg_cmd: return False

        # 5. Execute Registration
        return self._execute_registration(reg_cmd)

    def _create_sn_wallet(self, filename):
        """Creates wallet and returns address"""
        self.rpc.call(self.config.wallet_rpc_port, "create_wallet", {
            "filename": filename, "password": "dummy", "language": "English"
        })
        res = self.rpc.call(self.config.wallet_rpc_port, "get_address")
        if res and "result" in res:
            addr = res["result"]["address"]
            print(f"   📍 Created wallet {filename}: {addr}")
            return addr
        print("   ❌ Failed to get address")
        return None

    def _fund_wallet(self, destination_address, amount):
        """Switches to Genesis, WAITS for unlocked funds, then sends"""
        # 1. Switch to Genesis
        self.rpc.call(self.config.wallet_rpc_port, "open_wallet", {
            "filename": "genesis",
            "password": "dummy"
        })

        # 2. Calculate total needed (Amount + 1.0 XEQ for fees)
        transfer_amount = amount + 1000000000

        # 3. WAIT for Genesis to have enough unlocked money
        # This handles the "change lock" from the previous transaction
        print(f"   💰 Checking Genesis unlocked balance...")
        if not self._wait_for_unlock(transfer_amount):
            print("   ❌ Genesis wallet never unlocked enough funds.")
            return False

        # 4. Send the funds
        res = self.rpc.call(self.config.wallet_rpc_port, "transfer", {
            "destinations": [{"amount": transfer_amount, "address": destination_address}],
            "priority": 1,
            "ring_size": 16
        })

        if res and "result" in res:
            tx_hash = res["result"]["tx_hash"]
            print(f"   💸 Sent {amount} (atomic) to SN. Tx: {tx_hash}")
            return True

        print(f"   ❌ Transfer failed: {res}")
        return False

    def _wait_for_unlock(self, required_amount):
        """Polls the current wallet until unlocked_balance >= required"""
        max_retries = 60 # Wait up to ~2-3 minutes (testnet blocks are fast)

        for i in range(max_retries):
            self.rpc.call(self.config.wallet_rpc_port, "refresh")
            res = self.rpc.call(self.config.wallet_rpc_port, "get_balance")

            if res and "result" in res:
                unlocked = res["result"]["unlocked_balance"]
                total = res["result"]["balance"]

                if unlocked >= required_amount:
                    print(f"   ✅ Funds unlocked! Balance: {unlocked}")
                    return True

                # Optional: Print status every 5 attempts
                if i % 5 == 0:
                    print(f"      ...waiting for unlock. Current: {unlocked}/{required_amount} (Total: {total})")

            time.sleep(2)

        print("   ❌ Timed out waiting for funds to unlock.")
        return False

    def _get_registration_cmd(self, sn_rpc_port, sn_address):
        """Asks the Service Node Daemon for the registration string"""
        # Note: Using the portions logic from your original script
        STAKING_PORTIONS = 18446744073709551612

        params = {
            "operator_cut": "100.0",
            "contributor_addresses": [sn_address],
            "contributor_amounts": [STAKING_PORTIONS],
            "staking_requirement": self.config.staking_requirement
        }

        res = self.rpc.call(sn_rpc_port, "get_service_node_registration_cmd", params)

        if res and "result" in res:
            return res["result"]["registration_cmd"]

        print(f"   ❌ Failed to get registration command from port {sn_rpc_port}. Is the SN daemon running?")
        return None

    def _execute_registration(self, cmd):
        """Submits the registration command to the wallet"""
        print(f"   📝 Submitting registration transaction...")
        res = self.rpc.call(self.config.wallet_rpc_port, "register_service_node", {
            "register_service_node_str": cmd
        })

        if res and "result" in res:
            tx_hash = res["result"]["tx_hash"]
            print(f"   ✅ SUCCESS! Service Node Registered. Tx: {tx_hash}")
            return True

        print(f"   ❌ Registration failed: {res}")
        return False


class NetworkMonitor:
    """Monitors network status"""

    def __init__(self, rpc_client, wallet_manager, config):
        self.rpc = rpc_client
        self.wallet = wallet_manager
        self.config = config
        self.active = False
        self.thread = None

    def start(self):
        """Start monitoring"""
        self.active = True
        self.thread = threading.Thread(target=self._monitor_loop, daemon=True)
        self.thread.start()

    def stop(self):
        """Stop monitoring"""
        self.active = False
        if self.thread:
            self.thread.join(timeout=2)

    def _monitor_loop(self):
        """Background monitoring loop"""
        while self.active:
            try:
                # Get blockchain info
                daemon_info = self.rpc.call(self.config.daemon_rpc_port, "get_info")
                balance_info = self.wallet.get_balance()

                if daemon_info and "result" in daemon_info:
                    height = daemon_info["result"].get("height", 0)
                    difficulty = daemon_info["result"].get("difficulty", 0)
                    balance = balance_info.get("balance", 0)
                    unlocked = balance_info.get("unlocked_balance", 0)

                    print(f"📊 Block: {height} | Difficulty: {difficulty:,} | Balance: {balance:,} | Unlocked: {unlocked:,}")
            except:
                pass
            time.sleep(1)

class EquilibriaNetwork:
    """Main network orchestrator"""

    def __init__(self, service_nodes=20, regular_nodes=5):
        self.service_nodes = service_nodes
        self.regular_nodes = regular_nodes

        # Initialize components
        self.config = NetworkConfig()
        self.docker = DockerManager(self.config)
        self.rpc = RPCClient()
        self.wallet = WalletManager(self.rpc, self.config)
        self.registrar = ServiceNodeRegistrar(self.rpc, self.wallet, self.config)
        self.monitor = NetworkMonitor(self.rpc, self.wallet, self.config)

        self._setup_directories()

    def _setup_directories(self):
        """Create necessary directories"""
        dirs = ["./data/bootstrap", "./wallets"]

        for i in range(1, self.service_nodes + 1):
            dirs.append(f"./data/sn{i:02d}")
        for i in range(1, self.regular_nodes + 1):
            dirs.append(f"./data/regular{i:02d}")

        for dir_path in dirs:
            Path(dir_path).mkdir(parents=True, exist_ok=True)

    def cleanup_containers(self):
        """Clean up all containers"""
        containers = ["bootstrap", "wallet-rpc"]
        containers.extend([f"sn{i:02d}" for i in range(1, self.service_nodes + 1)])
        containers.extend([f"regular{i:02d}" for i in range(1, self.regular_nodes + 1)])
        self.docker.cleanup_all_containers(containers)

    def start_bootstrap(self):
        """Start bootstrap node"""
        print("🚀 Starting bootstrap node...")
        cmd = [
            "docker", "run", "-dit", "--name", "bootstrap", "--network", "host",
            "-v", f"{os.getcwd()}/data/bootstrap:/data", "equilibria-node",
            "--testnet",
            "--dev-allow-local-ips",
            f"--fixed-difficulty={self.config.difficulty}",
            "--data-dir=/data",
            # --- FIXES ---
            "--p2p-bind-ip=127.0.0.1",  # Only listen on localhost
            "--p2p-bind-port=18080",
            "--rpc-bind-port=18081",
            "--out-peers=0",            # Don't dial out
            "--no-igd",                 # Don't map ports on router
            "--hide-my-port",           # Don't advertise to DHT
            # -------------
            "--log-level=1"
        ]
        return self.docker.run_command(cmd) is not None


    def start_wallet_rpc(self):
        """Start wallet RPC"""
        print("💳 Starting wallet RPC...")
        cmd = [
            "docker", "run", "-d", "--name", "wallet-rpc", "--network", "host",
            "--entrypoint", "/usr/local/bin/xeq-wallet-rpc",
            "-v", f"{os.getcwd()}/wallets:/data", "equilibria-node",
            "--testnet", "--rpc-bind-port=18084",
            "--daemon-address=127.0.0.1:18081", "--disable-rpc-login",
            "--password=dummy", "--wallet-dir=/data", "--log-level=1"
        ]
        return self.docker.run_command(cmd) is not None

    def start_service_nodes(self):
        """Start all service nodes"""
        print(f"🔐 Starting {self.service_nodes} service nodes...")

        for i in range(1, self.service_nodes + 1):
            p2p_port = 18090 + (i-1) * 2
            rpc_port = 18091 + (i-1) * 2
            quorumnet_port = 38160 + (i-1)  # Unique quorumnet port for each SN

            cmd = [
                "docker", "run", "-dit", "--name", f"sn{i:02d}", "--network", "host",
                "-v", f"{os.getcwd()}/data/sn{i:02d}:/data", "equilibria-node",
                "--testnet", "--dev-allow-local-ips", "--service-node", f"--fixed-difficulty={self.config.difficulty}",
                "--data-dir=/data", f"--p2p-bind-port={p2p_port}",
                f"--rpc-bind-port={rpc_port}", "--add-priority-node=127.0.0.1:18080",
                "--service-node-public-ip=127.0.0.1",
                "--l2-provider=http://dummy-provider",
                f"--quorumnet-port={quorumnet_port}",  # Use this instead of --omq-port
                "--log-level=3"
            ]

            if self.docker.run_command(cmd):
                print(f"    ✅ SN{i:02d} started (ports: P2P={p2p_port}, RPC={rpc_port}, OMQ={quorumnet_port})")
            time.sleep(0.5)

    def start_regular_nodes(self):
        """Start all regular nodes"""
        print(f"📡 Starting {self.regular_nodes} regular nodes...")

        for i in range(1, self.regular_nodes + 1):
            p2p_port = 18150 + (i-1) * 2
            rpc_port = 18151 + (i-1) * 2

            cmd = [
                "docker", "run", "-dit", "--name", f"regular{i:02d}", "--network", "host",
                "-v", f"{os.getcwd()}/data/regular{i:02d}:/data", "equilibria-node",
                "--testnet", "--dev-allow-local-ips",
                f"--fixed-difficulty={self.config.difficulty}", "--data-dir=/data",
                f"--p2p-bind-port={p2p_port}", f"--rpc-bind-port={rpc_port}",
                "--add-priority-node=127.0.0.1:18080", "--log-level=1"
            ]

            self.docker.run_command(cmd)
            time.sleep(0.5)

    def start_mining(self):
        """Start mining"""
        print("⛏️  Starting mining...")
        result = self.rpc.call(self.config.daemon_rpc_port, "start_mining", {
            "miner_address": self.config.genesis_address,
            "threads_count": 4,
            "do_background_mining": False,
            "ignore_battery": True
        })
        return result and "error" not in result

    def stop_mining(self):
        """Stop mining on bootstrap node"""
        print("🛑 Stopping PoW mining on bootstrap node...")
        result = self.rpc.call(self.config.daemon_rpc_port, "stop_mining")
        if result and "error" not in result:
            print("✅ PoW mining stopped successfully")
            return True
        else:
            print(f"⚠️  Failed to stop mining: {result}")
            return False

    def wait_for_blocks(self, target_height):
        """Wait for blockchain to reach target height"""
        print(f"⏳ Waiting for block {target_height}...")

        while True:
            result = self.rpc.call(self.config.daemon_rpc_port, "get_info")
            if result and "result" in result:
                height = result["result"].get("height", 0)
                if height >= target_height:
                    print(f"✅ Reached block {height}!")
                    return True
            time.sleep(2)

    def wait_for_pos_activation(self, pos_blocks_required=3):
        """Wait for HF16 activation and confirm PoS blocks are being produced"""
        print(f"\n⏳ Waiting for PoS activation (HF16 at block {self.config.hf16_height})...")
        
        # First, wait for HF16 height
        self.wait_for_blocks(self.config.hf16_height)
        print(f"✅ HF16 height reached at block {self.config.hf16_height}")
        
        # Now verify PoS blocks are being produced
        print(f"🔍 Verifying PoS block production (need {pos_blocks_required} consecutive PoS blocks)...")
        
        pos_block_count = 0
        last_height = self.config.hf16_height
        
        while pos_block_count < pos_blocks_required:
            time.sleep(3)
            
            result = self.rpc.call(self.config.daemon_rpc_port, "get_info")
            if not result or "result" not in result:
                continue
                
            current_height = result["result"].get("height", 0)
            
            # Check if new blocks have been produced
            if current_height > last_height:
                # Get the last block header to check if it's a PoS block
                block_result = self.rpc.call(self.config.daemon_rpc_port, "get_last_block_header")
                
                if block_result and "result" in block_result:
                    block_header = block_result["result"].get("block_header", {})
                    
                    # PoS blocks have a reward of 0 (service nodes get rewards differently)
                    # PoW blocks have a non-zero reward
                    reward = block_header.get("reward", 0)
                    
                    if reward == 0:
                        pos_block_count += 1
                        print(f"   ✅ PoS block detected at height {current_height} ({pos_block_count}/{pos_blocks_required})")
                    else:
                        # Reset counter if we see a PoW block
                        pos_block_count = 0
                        print(f"   ⚠️  PoW block detected at height {current_height}, resetting counter")
                
                last_height = current_height
        
        print(f"✅ PoS network is active! {pos_blocks_required} consecutive PoS blocks confirmed")
        return True

    def create_dummy_transactions(self, count=20):
        """Create dummy transactions to populate output pool for ring signatures"""
        print(f"🔄 Creating {count} dummy transactions to populate output pool...")

        # Ensure genesis wallet is open
        self.rpc.call(self.config.wallet_rpc_port, "open_wallet", {
            "filename": "genesis",
            "password": "dummy"
        })
        time.sleep(1)

        # First, sweep dust to consolidate small outputs
        print("🧹 Sweeping dust first...")
        sweep_result = self.rpc.call(self.config.wallet_rpc_port, "sweep_dust", {
            "get_tx_keys": True
        })
        if sweep_result and "result" in sweep_result:
            print("    ✅ Dust swept successfully")
            time.sleep(5)  # Wait for sweep to be mined

        # Refresh wallet
        self.rpc.call(self.config.wallet_rpc_port, "refresh")
        time.sleep(3)

        # Create transactions with varying amounts to generate diverse outputs
        amounts = [
            500000000,   # 0.5 XEQ
            1000000000,  # 1 XEQ
            2000000000,  # 2 XEQ
            5000000000,  # 5 XEQ
            10000000000, # 10 XEQ
        ]

        for i in range(count):
            amount = amounts[i % len(amounts)]  # Cycle through different amounts

            result = self.rpc.call(self.config.wallet_rpc_port, "transfer", {
                "destinations": [{
                    "amount": amount,
                    "address": self.config.genesis_address
                }],
                "priority": 1,
                "ring_size": 2,  # Use minimum ring size initially
                "get_tx_key": True
            })

            if result and "result" in result:
                print(f"    ✅ Dummy tx {i+1}/{count} created ({amount:,} atomic)")
            else:
                print(f"    ❌ Dummy tx {i+1}/{count} failed: {result}")
                # If it fails, try with sweep_all instead
                if "Not enough outputs" in str(result):
                    break

            time.sleep(1)

        print(f"📊 Dummy transaction creation complete")
        time.sleep(10)  # Wait for mining

        self.rpc.call(self.config.wallet_rpc_port, "refresh")
        time.sleep(3)

    def start_network(self):
        """Start the complete network"""
        print("🚀 Starting Equilibria Network...")
        print(f"   Service nodes: {self.service_nodes}")
        print(f"   Regular nodes: {self.regular_nodes}")
        print(f"   HF16 activation: Block {self.config.hf16_height}")
        print()

        # 1. Start Core Infrastructure
        if not self.start_bootstrap():
            print("❌ Failed to start bootstrap node")
            return False

        if not self.rpc.wait_for_service(self.config.daemon_rpc_port):
            print("❌ Bootstrap node failed to start")
            return False

        if not self.start_wallet_rpc():
            print("❌ Failed to start wallet RPC")
            return False

        if not self.rpc.wait_for_service(self.config.wallet_rpc_port):
            print("❌ Wallet RPC failed to start")
            return False

        # 2. Start Network Nodes
        self.start_service_nodes()
        self.start_regular_nodes()  # UNCOMMENTED: Start regular nodes

        print("⏳ Waiting 15s for P2P connections to stabilize...")
        time.sleep(15)  # UNCOMMENTED: Give docker containers time to spin up

        # 3. Setup Genesis Wallet & Mining
        if not self.wallet.setup_genesis_wallet():
            print("❌ Failed to setup genesis wallet")
            return False

        if not self.start_mining():
            print("❌ Failed to start mining")
            return False

        # 4. Wait for Coinbase Unlock
        # We wait for unlock_window + a buffer to ensure we have spendable funds
        print(f"⏳ Mining blocks to unlock genesis funds (Target: {self.config.unlock_window + 5})...")
        self.wait_for_blocks(self.config.unlock_window + 5)

        # 5. Create Dummy Transactions (CRITICAL)
        # We need to populate the chain with outputs so Ring Signatures work.
        # Without this, transfers will fail with "Not enough outputs".
        self.create_dummy_transactions(15)

        # 6. Register Service Nodes
        self.monitor.start()

        registered_count = 0
        print(f"\n🏁 Starting Registration Loop for {self.service_nodes} nodes...")

        for i in range(1, self.service_nodes + 1):
            # Using the new robust register_node method
            if self.registrar.register_node(i):
                registered_count += 1
            else:
                print(f"⚠️ Critical failure registering SN{i:02d}. Stopping sequence.")
                break

        self.monitor.stop()

        # 7. Wait for PoS activation and stop PoW mining
        if registered_count > 0:
            print(f"\n🔄 Transitioning to PoS consensus...")
            if self.wait_for_pos_activation(pos_blocks_required=3):
                self.stop_mining()
            else:
                print("⚠️  PoS activation verification failed, but continuing...")

        print("\n🎉 Network setup complete!")
        print(f"   Bootstrap: http://127.0.0.1:18081")
        print(f"   Wallet RPC: http://127.0.0.1:18084")
        print(f"   Service nodes Registered: {registered_count}/{self.service_nodes}")

        return True
