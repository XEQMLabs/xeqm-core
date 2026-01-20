import subprocess
import time
import json
import requests
import argparse
import os
from pathlib import Path
import threading
import signal
import logging

class EmojiFormatter(logging.Formatter):
    """Add emojis to differentiate log levels"""
    
    EMOJIS = {
        logging.DEBUG: "🔍",
        logging.INFO: "ℹ️ ",
        logging.WARNING: "⚠️ ",
        logging.ERROR: "❌",
        logging.CRITICAL: "🔥"
    }

    def format(self, record):
        emoji = self.EMOJIS.get(record.levelno, "")
        record.msg = f"{emoji} {record.msg}"
        return super().format(record)

# Configure logging with emoji formatter
handler = logging.StreamHandler()
handler.setFormatter(EmojiFormatter(
    '%(asctime)s - %(name)s - %(levelname)s - %(message)s',
    datefmt='%Y-%m-%d %H:%M:%S'
))

logging.basicConfig(
    level=logging.INFO,
    handlers=[handler]
)

logger = logging.getLogger(__name__)

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
        self.eth_node_port = 8545  # Ethereum node port

class DockerManager:
    """Handles Docker container operations"""

    def __init__(self, config):
        self.config = config
        self.logger = logging.getLogger(f"{__name__}.DockerManager")

    def run_command(self, cmd):
        """Run docker command with error handling"""
        try:
            result = subprocess.run(cmd, capture_output=True, text=True, check=False)

            if result.returncode != 0:
                self.logger.error(f"Docker command failed: {' '.join(cmd)}")
                self.logger.error(f"Exit code: {result.returncode}")
                self.logger.error(f"STDERR: {result.stderr}")
                self._cleanup_failed_container(cmd)
                return None

            if result.stdout:
                self.logger.debug(f"Docker output: {result.stdout.strip()}")
            return result

        except Exception as e:
            self.logger.error(f"Docker exception: {e}")
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
        self.logger.info(f"Cleaned up {len(container_names)} containers")

class EthereumNodeManager:
    """Manages external Ethereum node process"""

    def __init__(self, config, node_directory=None):
        self.config = config
        self.node_directory = node_directory or os.getcwd()
        self.node_process = None
        self.logger = logging.getLogger(f"{__name__}.EthereumNodeManager")

    def start_node(self):
        """Start the Ethereum node using 'make node' in background"""
        self.logger.info(f"Starting Ethereum node in {self.node_directory}")

        try:
            log_file = open(os.path.join(self.node_directory, 'l2.log'), 'w')

            # Start the node process
            self.node_process = subprocess.Popen(
                ["make", "node"],
                cwd=self.node_directory,
                stdout=log_file,
                stderr=subprocess.STDOUT,
                preexec_fn=os.setsid  # Create new process group
            )

            self.logger.info(f"Node process started with PID {self.node_process.pid}")
            self.logger.debug(f"Output being written to {self.node_directory}/l2.log")

            # Wait for node to be ready
            self.logger.info("Waiting 30 seconds for node to initialize...")
            time.sleep(30)

            # Verify node is running
            if self.node_process.poll() is not None:
                self.logger.error("Node process terminated unexpectedly")
                return False

            self.logger.info("Node is ready")
            return True

        except Exception as e:
            self.logger.error(f"Failed to start node: {e}")
            return False

    def deploy_contracts(self):
        """Deploy contracts using 'make deploy-local'"""
        self.logger.info("Deploying contracts...")

        try:
            result = subprocess.run(
                ["make", "deploy-local"],
                cwd=self.node_directory,
                capture_output=True,
                text=True,
                check=False
            )

            if result.returncode == 0:
                self.logger.info("Contracts deployed successfully")
                self.logger.debug(result.stdout)
                return True
            else:
                self.logger.error("Contract deployment failed")
                self.logger.error(result.stderr)
                return False

        except Exception as e:
            self.logger.error(f"Failed to deploy contracts: {e}")
            return False

    def stop_node(self):
        """Stop the Ethereum node process"""
        if self.node_process:
            self.logger.info("Stopping Ethereum node...")
            try:
                # Kill the entire process group
                os.killpg(os.getpgid(self.node_process.pid), signal.SIGTERM)
                self.node_process.wait(timeout=10)
                self.logger.info("Ethereum node stopped")
            except Exception as e:
                self.logger.warning(f"Error stopping node: {e}")
                try:
                    os.killpg(os.getpgid(self.node_process.pid), signal.SIGKILL)
                except:
                    pass

class RPCClient:
    """Handles RPC communication with daemon and wallet"""

    def __init__(self):
        self.logger = logging.getLogger(f"{__name__}.RPCClient")

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
            self.logger.error(f"RPC call failed: {method} on port {port} - {e}")
            return None

    def wait_for_service(self, port, timeout=60):
        """Wait for service to be ready"""
        self.logger.debug(f"Waiting for service on port {port}")
        start_time = time.time()
        while time.time() - start_time < timeout:
            try:
                response = requests.post(
                    f"http://127.0.0.1:{port}/json_rpc",
                    json={"jsonrpc": "2.0", "id": "0", "method": "get_info"},
                    timeout=5
                )
                if response.status_code == 200:
                    self.logger.debug(f"Service on port {port} is ready")
                    return True
            except:
                pass
            time.sleep(2)
        self.logger.error(f"Service on port {port} failed to start within {timeout}s")
        return False

class WalletManager:
    """Handles wallet operations"""

    def __init__(self, rpc_client, config):
        self.rpc = rpc_client
        self.config = config
        self.logger = logging.getLogger(f"{__name__}.WalletManager")

    def setup_genesis_wallet(self):
        """Create and setup genesis wallet"""
        self.logger.info("Setting up genesis wallet")

        result = self.rpc.call(self.config.wallet_rpc_port, "generate_from_keys", {
            "filename": "genesis",
            "password": "dummy",
            "address": self.config.genesis_address,
            "spendkey": self.config.genesis_spend_key,
            "viewkey": self.config.genesis_view_key,
            "restore_height": 0
        })

        if not result or "error" in result:
            self.logger.error(f'Failed to generate genesis wallet: {result}')
            return False

        self.logger.info("Rescanning blockchain...")
        self.rpc.call(self.config.wallet_rpc_port, "rescan_blockchain")
        time.sleep(5)
        self.refresh()
        return True

    def refresh(self):
        """Refresh wallet state"""
        self.logger.debug("Refreshing wallet")
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
        self.logger.info(f"Waiting for {required_amount:,} unlocked balance")

        while True:
            self.refresh()
            balance_info = self.get_balance()
            unlocked = balance_info.get("unlocked_balance", 0)

            if unlocked >= required_amount:
                self.logger.info(f"Sufficient unlocked balance: {unlocked:,}")
                return True

            blocks_to_unlock = balance_info.get("blocks_to_unlock", 0)
            self.logger.debug(f"Unlocked: {unlocked:,}, need: {required_amount:,}, blocks to unlock: {blocks_to_unlock}")
            time.sleep(5)

class ServiceNodeRegistrar:
    """Handles service node registration with robust funding and unlocking logic"""

    def __init__(self, rpc_client, wallet_manager, config):
        self.rpc = rpc_client
        self.wallet = wallet_manager
        self.config = config
        self.logger = logging.getLogger(f"{__name__}.ServiceNodeRegistrar")

    def register_node(self, node_id):
        """Orchestrates the full registration flow for a single node"""
        self.logger.info(f"Starting automation for Service Node {node_id:02d}")

        # 1. Create the SN Wallet
        sn_wallet_name = f"sn{node_id:02d}"
        sn_address = self._create_sn_wallet(sn_wallet_name)
        if not sn_address: return False

        # 2. Fund the SN Wallet from Genesis
        self.logger.info(f"Funding {sn_wallet_name}")
        if not self._fund_wallet(sn_address, self.config.staking_requirement):
            return False

        # 3. Switch back to SN Wallet and WAIT for unlock
        # We need to be in the SN wallet to check its balance
        self.rpc.call(self.config.wallet_rpc_port, "open_wallet", {"filename": sn_wallet_name, "password": "dummy"})

        self.logger.info(f"Waiting for funds to unlock in {sn_wallet_name} (approx 10 blocks)")
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
            self.logger.info(f"Created wallet {filename}: {addr}")
            return addr
        self.logger.error(f"Failed to get address for {filename}")
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
        self.logger.debug("Checking Genesis unlocked balance")
        if not self._wait_for_unlock(transfer_amount):
            self.logger.error("Genesis wallet never unlocked enough funds")
            return False

        # 4. Send the funds
        res = self.rpc.call(self.config.wallet_rpc_port, "transfer", {
            "destinations": [{"amount": transfer_amount, "address": destination_address}],
            "priority": 1,
            "ring_size": 16
        })

        if res and "result" in res:
            tx_hash = res["result"]["tx_hash"]
            self.logger.info(f"Sent {amount} (atomic) to SN. Tx: {tx_hash}")
            return True

        self.logger.error(f"Transfer failed: {res}")
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
                    self.logger.info(f"Funds unlocked! Balance: {unlocked}")
                    return True

                # Optional: Print status every 5 attempts
                if i % 5 == 0:
                    self.logger.debug(f"Waiting for unlock. Current: {unlocked}/{required_amount} (Total: {total})")

            time.sleep(2)

        self.logger.error("Timed out waiting for funds to unlock")
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

        self.logger.error(f"Failed to get registration command from port {sn_rpc_port}. Is the SN daemon running?")
        return None

    def _execute_registration(self, cmd):
        """Submits the registration command to the wallet"""
        self.logger.info("Submitting registration transaction")
        res = self.rpc.call(self.config.wallet_rpc_port, "register_service_node", {
            "register_service_node_str": cmd
        })

        if res and "result" in res:
            tx_hash = res["result"]["tx_hash"]
            self.logger.info(f"SUCCESS! Service Node Registered. Tx: {tx_hash}")
            return True

        self.logger.error(f"Registration failed: {res}")
        return False


class NetworkMonitor:
    """Monitors network status"""

    def __init__(self, rpc_client, wallet_manager, config):
        self.rpc = rpc_client
        self.wallet = wallet_manager
        self.config = config
        self.active = False
        self.thread = None
        self.logger = logging.getLogger(f"{__name__}.NetworkMonitor")

    def start(self):
        """Start monitoring"""
        self.active = True
        self.thread = threading.Thread(target=self._monitor_loop, daemon=True)
        self.thread.start()
        self.logger.debug("Network monitoring started")

    def stop(self):
        """Stop monitoring"""
        self.active = False
        if self.thread:
            self.thread.join(timeout=2)
        self.logger.debug("Network monitoring stopped")

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

                    self.logger.info(f"Block: {height} | Difficulty: {difficulty:,} | Balance: {balance:,} | Unlocked: {unlocked:,}")
            except:
                pass
            time.sleep(1)

class EquilibriaNetwork:
    """Main network orchestrator"""

    def __init__(self, service_nodes=20, regular_nodes=5, eth_node_directory=None):
        self.service_nodes = service_nodes
        self.regular_nodes = regular_nodes

        # Initialize components
        self.config = NetworkConfig()
        self.docker = DockerManager(self.config)
        self.rpc = RPCClient()
        self.wallet = WalletManager(self.rpc, self.config)
        self.registrar = ServiceNodeRegistrar(self.rpc, self.wallet, self.config)
        self.monitor = NetworkMonitor(self.rpc, self.wallet, self.config)
        self.eth_node = EthereumNodeManager(self.config, eth_node_directory)
        self.logger = logging.getLogger(f"{__name__}.EquilibriaNetwork")

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

    def cleanup(self):
        """Clean up all resources"""
        self.logger.info("Cleaning up resources")
        self.cleanup_containers()
        self.eth_node.stop_node()

    def start_bootstrap(self):
        """Start bootstrap node"""
        self.logger.info("Starting bootstrap node")
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
        self.logger.info("Starting wallet RPC")
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
        self.logger.info(f"Starting {self.service_nodes} service nodes")

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
                self.logger.info(f"SN{i:02d} started (ports: P2P={p2p_port}, RPC={rpc_port}, OMQ={quorumnet_port})")
            time.sleep(0.5)

    def start_regular_nodes(self):
        """Start all regular nodes"""
        self.logger.info(f"Starting {self.regular_nodes} regular nodes")

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
        self.logger.info("Starting mining")
        result = self.rpc.call(self.config.daemon_rpc_port, "start_mining", {
            "miner_address": self.config.genesis_address,
            "threads_count": 4,
            "do_background_mining": False,
            "ignore_battery": True
        })
        return result and "error" not in result

    def stop_mining(self):
        """Stop mining on bootstrap node"""
        self.logger.info("Stopping PoW mining on bootstrap node")
        result = self.rpc.call(self.config.daemon_rpc_port, "stop_mining")
        if result and "error" not in result:
            self.logger.info("PoW mining stopped successfully")
            return True
        else:
            self.logger.warning(f"Failed to stop mining: {result}")
            return False

    def wait_for_blocks(self, target_height):
        """Wait for blockchain to reach target height"""
        self.logger.info(f"Waiting for block {target_height}")

        while True:
            result = self.rpc.call(self.config.daemon_rpc_port, "get_info")
            if result and "result" in result:
                height = result["result"].get("height", 0)
                if height >= target_height:
                    self.logger.info(f"Reached block {height}")
                    return True
            time.sleep(2)

    def wait_for_hf16_and_stop_mining(self):
        """Wait for HF16 activation and stop PoW mining"""
        self.logger.info(f"Waiting for HF16 activation at block {self.config.hf16_height}")
        
        # Wait for HF16 height
        self.wait_for_blocks(self.config.hf16_height)
        self.logger.info(f"HF16 height reached at block {self.config.hf16_height}")
        
        # Stop PoW mining now that PoS is active
        self.stop_mining()
        
        return True

    def create_dummy_transactions(self, count=20):
        """Create dummy transactions to populate output pool for ring signatures"""
        self.logger.info(f"Creating {count} dummy transactions to populate output pool")

        # Ensure genesis wallet is open
        self.rpc.call(self.config.wallet_rpc_port, "open_wallet", {
            "filename": "genesis",
            "password": "dummy"
        })
        time.sleep(1)

        # First, sweep dust to consolidate small outputs
        self.logger.info("Sweeping dust first")
        sweep_result = self.rpc.call(self.config.wallet_rpc_port, "sweep_dust", {
            "get_tx_keys": True
        })
        if sweep_result and "result" in sweep_result:
            self.logger.info("Dust swept successfully")
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
                self.logger.debug(f"Dummy tx {i+1}/{count} created ({amount:,} atomic)")
            else:
                self.logger.warning(f"Dummy tx {i+1}/{count} failed: {result}")
                # If it fails, try with sweep_all instead
                if "Not enough outputs" in str(result):
                    break

            time.sleep(1)

        self.logger.info("Dummy transaction creation complete")
        time.sleep(10)  # Wait for mining

        self.rpc.call(self.config.wallet_rpc_port, "refresh")
        time.sleep(3)

    def start_network(self):
        """Start the complete network"""
        self.logger.info("=" * 60)
        self.logger.info("Starting Equilibria Network")
        self.logger.info(f"Service nodes: {self.service_nodes}")
        self.logger.info(f"Regular nodes: {self.regular_nodes}")
        self.logger.info(f"HF16 activation: Block {self.config.hf16_height}")
        self.logger.info("=" * 60)

        # 0. Setup Ethereum Node (if directory provided)
        if self.eth_node.node_directory:
            self.logger.info("Setting up Ethereum node")

            if not self.eth_node.start_node():
                self.logger.error("Failed to start Ethereum node")
                return False

            if not self.eth_node.deploy_contracts():
                self.logger.error("Failed to deploy contracts")
                return False

            self.logger.info("Ethereum node setup complete")

        # 1. Start Core Infrastructure
        if not self.start_bootstrap():
            self.logger.error("Failed to start bootstrap node")
            return False

        if not self.rpc.wait_for_service(self.config.daemon_rpc_port):
            self.logger.error("Bootstrap node failed to start")
            return False

        if not self.start_wallet_rpc():
            self.logger.error("Failed to start wallet RPC")
            return False

        if not self.rpc.wait_for_service(self.config.wallet_rpc_port):
            self.logger.error("Wallet RPC failed to start")
            return False

        # 2. Start Network Nodes
        self.start_service_nodes()
        self.start_regular_nodes()

        self.logger.info("Waiting 15s for P2P connections to stabilize")
        time.sleep(15)

        # 3. Setup Genesis Wallet & Mining
        if not self.wallet.setup_genesis_wallet():
            self.logger.error("Failed to setup genesis wallet")
            return False

        if not self.start_mining():
            self.logger.error("Failed to start mining")
            return False

        # 4. Wait for Coinbase Unlock
        self.logger.info(f"Mining blocks to unlock genesis funds (Target: {self.config.unlock_window + 5})")
        self.wait_for_blocks(self.config.unlock_window + 5)

        # 5. Create Dummy Transactions (CRITICAL)
        # We need to populate the chain with outputs so Ring Signatures work.
        # Without this, transfers will fail with "Not enough outputs".
        self.create_dummy_transactions(15)

        # 6. Register Service Nodes
        self.monitor.start()

        registered_count = 0
        self.logger.info(f"Starting Registration Loop for {self.service_nodes} nodes")

        for i in range(1, self.service_nodes + 1):
            if self.registrar.register_node(i):
                registered_count += 1
            else:
                self.logger.error(f"Critical failure registering SN{i:02d}. Stopping sequence.")
                break

        self.monitor.stop()

        # 7. Wait for HF16 and stop PoW mining
        if registered_count > 0:
            self.logger.info("Transitioning to PoS consensus")
            self.wait_for_hf16_and_stop_mining()

        self.logger.info("=" * 60)
        self.logger.info("Network setup complete!")
        self.logger.info(f"Bootstrap: http://127.0.0.1:18081")
        self.logger.info(f"Wallet RPC: http://127.0.0.1:18084")
        if self.eth_node.node_directory:
            self.logger.info(f"Ethereum Node: http://127.0.0.1:{self.config.eth_node_port}")
        self.logger.info(f"Service nodes Registered: {registered_count}/{self.service_nodes}")
        self.logger.info("=" * 60)

        return True
