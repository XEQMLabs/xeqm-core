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
    """Handles service node registration"""

    def __init__(self, rpc_client, wallet_manager, config):
        self.rpc = rpc_client
        self.wallet = wallet_manager
        self.config = config

    def register_all_nodes(self, node_count):
        """Register all service nodes with retry logic"""
        print(f"🔐 Starting service node registration process...")

        # Wait for initial unlock
        self.wallet.wait_for_unlocked_balance(self.config.staking_requirement)

        registered_nodes = set()
        max_attempts = 5
        attempt = 0
        retry_interval = 2

        while len(registered_nodes) < node_count and attempt < max_attempts:
            attempt += 1
            print(f"\n📋 Registration attempt {attempt}/{max_attempts}")

            for i in range(1, node_count + 1):
                if i in registered_nodes:
                    continue

                if self._register_single_node(i):
                    registered_nodes.add(i)
                    print(f"✅ SN{i:02d} registered successfully")
                else:
                    print(f"❌ SN{i:02d} registration failed, will retry")

            if len(registered_nodes) < node_count:
                print(f"⏳ Waiting {retry_interval} seconds before retry... ({len(registered_nodes)}/{node_count} registered)")
                time.sleep(retry_interval)

        print(f"\n📊 Final result: {len(registered_nodes)}/{node_count} service nodes registered")
        return len(registered_nodes)

    def _register_single_node(self, node_id):
        """Register a single service node"""
        try:
            print(f"🔄 Registering SN{node_id:02d}...")

            # Create service node wallet
            wallet_name = f"sn{node_id:02d}"
            result = self.rpc.call(self.config.wallet_rpc_port, "create_wallet", {
                "filename": wallet_name,
                "password": "dummy",
                "language": "English"
            })

            if not result or "error" in result:
                print(f"❌ Failed to create wallet for SN{node_id:02d}")
                return False

            time.sleep(1)

            # Get service node address
            result = self.rpc.call(self.config.wallet_rpc_port, "get_address")
            if not result or "result" not in result:
                print(f"❌ Failed to get address for SN{node_id:02d}")
                return False

            sn_address = result["result"]["address"]
            print(f"📍 SN{node_id:02d} address: {sn_address}")

            # Switch back to genesis wallet for transfer
            self.rpc.call(self.config.wallet_rpc_port, "open_wallet", {
                "filename": "genesis",
                "password": "dummy"
            })
            time.sleep(1)

            # Transfer staking amount
            result = self.rpc.call(self.config.wallet_rpc_port, "transfer", {
                "destinations": [{
                    "amount": self.config.staking_requirement,
                    "address": sn_address
                }],
                "priority": 1
            })

            if not result or "result" not in result:
                print(f"❌ Failed to transfer to SN{node_id:02d}")
                return False

            print(f"💸 Transferred {self.config.staking_requirement:,} to SN{node_id:02d}")
            time.sleep(5)

            # Switch to service node wallet and refresh
            self.rpc.call(self.config.wallet_rpc_port, "open_wallet", {
                "filename": wallet_name,
                "password": "dummy"
            })
            time.sleep(1)
            self.rpc.call(self.config.wallet_rpc_port, "refresh")
            time.sleep(2)

            # Get service node RPC port and registration command
            sn_rpc_port = 18091 + (node_id-1) * 2

            print(f"🔍 Calling SN{node_id:02d} at port {sn_rpc_port}")
            print(f"🔍 Using address: {sn_address}")
            print(f"🔍 Staking requirement: {self.config.staking_requirement}")

            # registration_cmd_result = self.rpc.call(sn_rpc_port, "get_service_node_registration_cmd", {
            #     "operator_cut": "100.0",
            #     "contributor_addresses": [sn_address],
            #     "contributor_amounts": [self.config.staking_requirement],
            #     "staking_requirement": self.config.staking_requirement
            # })

            # Use portions instead of atomic units for HF < 19
            STAKING_PORTIONS = 18446744073709551612  # Total portions available (from your logs)

            registration_cmd_result = self.rpc.call(sn_rpc_port, "get_service_node_registration_cmd", {
                "operator_cut": "100.0",
                "contributor_addresses": [sn_address],
                "contributor_amounts": [STAKING_PORTIONS],  # Use full portions for 100% stake
                "staking_requirement": self.config.staking_requirement
            })

            if not registration_cmd_result or "error" in registration_cmd_result:
                print(f"❌ Failed to get registration cmd: {registration_cmd_result}")
                return False

            registration_cmd = registration_cmd_result["result"]["registration_cmd"]

            # Submit registration via service node wallet
            result = self.rpc.call(self.config.wallet_rpc_port, "register_service_node", {
                "register_service_node_str": registration_cmd
            })

            if not result or "error" in result:
                print(f"❌ SN{node_id:02d} registration failed: {result}")
                return False

            print(f"✅ SN{node_id:02d} registered successfully")
            return True

        except Exception as e:
            print(f"❌ Exception registering SN{node_id:02d}: {e}")
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
            "--testnet", "--dev-allow-local-ips", "--offline", "--no-sync",
            f"--fixed-difficulty={self.config.difficulty}", "--data-dir=/data",
            "--p2p-bind-port=18080", "--rpc-bind-port=18081", "--log-level=1"
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
                "--testnet", "--dev-allow-local-ips", "--service-node",
                "--offline", "--no-sync", f"--fixed-difficulty={self.config.difficulty}",
                "--data-dir=/data", f"--p2p-bind-port={p2p_port}",
                f"--rpc-bind-port={rpc_port}", "--add-exclusive-node=127.0.0.1:18080",
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
                "--testnet", "--dev-allow-local-ips", "--offline", "--no-sync",
                f"--fixed-difficulty={self.config.difficulty}", "--data-dir=/data",
                f"--p2p-bind-port={p2p_port}", f"--rpc-bind-port={rpc_port}",
                "--add-exclusive-node=127.0.0.1:18080", "--log-level=1"
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

        # Start core services
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

        # Start nodes
        self.start_service_nodes()
        self.start_regular_nodes()

        print("⏳ Waiting for nodes to sync...")
        time.sleep(10)

        # Setup wallet and mining
        if not self.wallet.setup_genesis_wallet():
            print("❌ Failed to setup genesis wallet")
            return False

        if not self.start_mining():
            print("❌ Failed to start mining")
            return False

        # Wait for coins to unlock
        self.wait_for_blocks(self.config.unlock_window)

        # Create dummy transactions to populate output pool
        self.create_dummy_transactions(2)

        # Start monitoring and register service nodes
        self.monitor.start()
        registered = self.registrar.register_all_nodes(self.service_nodes)
        self.monitor.stop()

        print("\n🎉 Network setup complete!")
        print(f"   Bootstrap: http://127.0.0.1:18081")
        print(f"   Wallet RPC: http://127.0.0.1:18084")
        print(f"   Service nodes: {self.service_nodes} (registered: {registered})")
        print(f"   Regular nodes: {self.regular_nodes}")

        return True
