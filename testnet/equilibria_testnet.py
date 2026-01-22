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
from dataclasses import dataclass
from typing import List, Optional

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


@dataclass
class ServiceNodeInfo:
    """Information about a single service node"""
    pubkey: str
    pubkey_x25519: str
    active: bool
    funded: bool
    state_height: int
    last_uptime_proof: int
    public_ip: str
    quorumnet_port: int

    @property
    def pubkey_short(self) -> str:
        return self.pubkey[:16] + "..."


@dataclass
class ServiceNodeState:
    """Overall state of service nodes in the network"""
    total_registered: int
    active_count: int
    inactive_count: int
    funded_count: int
    nodes: List[ServiceNodeInfo]
    current_height: int
    pulse_min_required: int

    @property
    def pulse_ready(self) -> bool:
        return self.active_count >= self.pulse_min_required

    @property
    def quorum_possible(self) -> bool:
        return self.active_count >= 7  # Minimum for signatures

    def summary(self) -> str:
        """Return a formatted summary string"""
        lines = [
            "=" * 60,
            "SERVICE NODE STATE SUMMARY",
            "=" * 60,
            f"Current Height: {self.current_height}",
            f"Total Registered: {self.total_registered}",
            f"Active: {self.active_count}",
            f"Inactive: {self.inactive_count}",
            f"Funded: {self.funded_count}",
            f"Pulse Min Required: {self.pulse_min_required}",
            f"Pulse Ready: {'✅ YES' if self.pulse_ready else '❌ NO'}",
            f"Quorum Possible: {'✅ YES' if self.quorum_possible else '❌ NO'}",
            "-" * 60,
            "INDIVIDUAL NODES:",
            "-" * 60,
        ]

        for node in self.nodes:
            status = "🟢 ACTIVE" if node.active else "🔴 INACTIVE"
            lines.append(f"  {node.pubkey_short} | {status} | IP: {node.public_ip} | Height: {node.state_height}")

        lines.append("=" * 60)
        return "\n".join(lines)

    def __str__(self) -> str:
        return self.summary()


class NetworkConfig:
    """Network configuration constants"""
    def __init__(self):
        self.genesis_address = "XEQTN3HrcXx7oWPEyWVzyw1V1GQFkXkPzJ9g7LyuHm5D6xbPFrAE8MyK7ZiVBp11ic72YQZwo6UzF2Rc5EWbnEHT99VbHLUx18"
        self.genesis_spend_key = "e0e3bfa8113406541ad8765bf1dddbf5151da1ad6f7586af8686bc5e5e15470b"
        self.genesis_view_key = "f49c400d21ef3f12854e3377d467ff63ba2d6013fa85d465f3c807b716b1c60b"
        self.wallet_rpc_port = 18084
        self.daemon_rpc_port = 18081
        # Equilibria Horizon: Full stake requirement is 100,000 XEQ (in atomic units)
        self.staking_requirement = 100000000000000  # 100,000 XEQ in atomic units
        self.difficulty = 750
        self.unlock_window = 30  # Blocks needed for coin unlock
        self.hf16_height = 300   # HF16 activation height (Pulse)
        self.pulse_min_service_nodes = 12  # Minimum active SNs for Pulse
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
        """Make RPC call to 127.0.0.1"""
        return self.call_host("127.0.0.1", port, method, params)

    def call_host(self, host, port, method, params=None):
        """Make RPC call to specific host"""
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
                f"http://{host}:{port}/json_rpc",
                json=payload,
                timeout=30
            )
            return response.json()
        except Exception as e:
            self.logger.error(f"RPC call failed: {method} on {host}:{port} - {e}")
            return None

    def wait_for_service(self, port, timeout=60):
        """Wait for service to be ready"""
        return self.wait_for_service_host("127.0.0.1", port, timeout)

    def wait_for_service_host(self, host, port, timeout=60):
        """Wait for service to be ready on specific host"""
        self.logger.debug(f"Waiting for service on {host}:{port}")
        start_time = time.time()
        while time.time() - start_time < timeout:
            try:
                response = requests.post(
                    f"http://{host}:{port}/json_rpc",
                    json={"jsonrpc": "2.0", "id": "0", "method": "get_info"},
                    timeout=5
                )
                if response.status_code == 200:
                    self.logger.debug(f"Service on {host}:{port} is ready")
                    return True
            except:
                pass
            time.sleep(2)
        self.logger.error(f"Service on {host}:{port} failed to start within {timeout}s")
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
    """Handles service node registration with parallel funding"""

    def __init__(self, rpc_client, wallet_manager, config):
        self.rpc = rpc_client
        self.wallet = wallet_manager
        self.config = config
        self.logger = logging.getLogger(f"{__name__}.ServiceNodeRegistrar")

    def register_all_nodes_parallel(self, num_nodes):
        """
        Register all service nodes using parallel funding strategy:
        1. Create all wallets
        2. Fund all wallets from genesis
        3. Wait once for all funds to unlock
        4. Register all nodes
        """
        self.logger.info(f"Starting parallel registration for {num_nodes} service nodes")

        # Phase 1: Create all SN wallets and collect addresses
        self.logger.info("Phase 1: Creating all service node wallets")
        sn_data = []  # List of (node_id, wallet_name, address)

        for i in range(1, num_nodes + 1):
            sn_wallet_name = f"sn{i:02d}"
            sn_address = self._create_sn_wallet(sn_wallet_name)
            if sn_address:
                sn_data.append((i, sn_wallet_name, sn_address))
            else:
                self.logger.error(f"Failed to create wallet for SN{i:02d}")

        self.logger.info(f"Created {len(sn_data)}/{num_nodes} wallets")

        if len(sn_data) == 0:
            self.logger.error("No wallets created, aborting")
            return 0

        # Phase 2: Fund all wallets from genesis
        self.logger.info("Phase 2: Funding all service node wallets")

        # Open genesis wallet
        self.rpc.call(self.config.wallet_rpc_port, "open_wallet", {
            "filename": "genesis",
            "password": "dummy"
        })
        time.sleep(1)
        self.rpc.call(self.config.wallet_rpc_port, "refresh")
        time.sleep(2)

        funded_nodes = []
        for node_id, wallet_name, address in sn_data:
            if self._fund_wallet_no_wait(address, self.config.staking_requirement):
                funded_nodes.append((node_id, wallet_name, address))
                self.logger.info(f"Funded {wallet_name}")
            else:
                self.logger.error(f"Failed to fund {wallet_name}")
            # Small delay between transactions to avoid issues
            time.sleep(1)

        self.logger.info(f"Funded {len(funded_nodes)}/{len(sn_data)} wallets")

        if len(funded_nodes) == 0:
            self.logger.error("No wallets funded, aborting")
            return 0

        # Phase 3: Wait for all funds to unlock
        self.logger.info("Phase 3: Waiting for all funds to unlock")
        self._wait_for_all_unlocks(funded_nodes)

        # Phase 4: Register all nodes
        self.logger.info("Phase 4: Registering all service nodes")
        registered_count = 0

        for node_id, wallet_name, address in funded_nodes:
            if self._register_funded_node(node_id, wallet_name, address):
                registered_count += 1
            else:
                self.logger.error(f"Failed to register SN{node_id:02d}")

        self.logger.info(f"Successfully registered {registered_count}/{len(funded_nodes)} service nodes")
        return registered_count

    def _create_sn_wallet(self, filename):
        """Creates wallet and returns address"""
        self.rpc.call(self.config.wallet_rpc_port, "create_wallet", {
            "filename": filename, "password": "dummy", "language": "English"
        })
        res = self.rpc.call(self.config.wallet_rpc_port, "get_address")
        if res and "result" in res:
            addr = res["result"]["address"]
            self.logger.debug(f"Created wallet {filename}: {addr[:16]}...")
            return addr
        self.logger.error(f"Failed to get address for {filename}")
        return None

    def _fund_wallet_no_wait(self, destination_address, amount):
        """Send funds without waiting for unlock (for parallel funding)"""
        # Make sure genesis wallet is open
        self.rpc.call(self.config.wallet_rpc_port, "open_wallet", {
            "filename": "genesis",
            "password": "dummy"
        })

        # Refresh to get latest balance
        self.rpc.call(self.config.wallet_rpc_port, "refresh")

        # Calculate total needed (Amount + 1.0 XEQ for fees)
        transfer_amount = amount + 1000000000

        # Check if we have enough unlocked balance
        balance_info = self.wallet.get_balance()
        unlocked = balance_info.get("unlocked_balance", 0)

        if unlocked < transfer_amount:
            self.logger.warning(f"Insufficient unlocked balance: {unlocked:,} < {transfer_amount:,}, waiting...")
            if not self._wait_for_unlock_genesis(transfer_amount):
                return False

        # Send the funds
        res = self.rpc.call(self.config.wallet_rpc_port, "transfer", {
            "destinations": [{"amount": transfer_amount, "address": destination_address}],
            "priority": 1,
            "ring_size": 16
        })

        if res and "result" in res:
            tx_hash = res["result"]["tx_hash"]
            self.logger.debug(f"Sent {amount:,} atomic. Tx: {tx_hash[:16]}...")
            return True

        self.logger.error(f"Transfer failed: {res}")
        return False

    def _wait_for_unlock_genesis(self, required_amount):
        """Wait for genesis wallet to have enough unlocked funds"""
        max_retries = 120  # Up to ~4 minutes

        for i in range(max_retries):
            self.rpc.call(self.config.wallet_rpc_port, "open_wallet", {
                "filename": "genesis",
                "password": "dummy"
            })
            self.rpc.call(self.config.wallet_rpc_port, "refresh")
            res = self.rpc.call(self.config.wallet_rpc_port, "get_balance")

            if res and "result" in res:
                unlocked = res["result"]["unlocked_balance"]
                if unlocked >= required_amount:
                    return True
                if i % 10 == 0:
                    self.logger.debug(f"Genesis unlock wait: {unlocked:,}/{required_amount:,}")

            time.sleep(2)

        self.logger.error("Timed out waiting for genesis funds to unlock")
        return False

    def _wait_for_all_unlocks(self, funded_nodes):
        """Wait for all funded wallets to have unlocked balances"""
        self.logger.info(f"Waiting for {len(funded_nodes)} wallets to unlock...")

        pending = set(node_id for node_id, _, _ in funded_nodes)
        max_retries = 120  # Up to ~4 minutes

        for attempt in range(max_retries):
            still_pending = set()

            for node_id, wallet_name, _ in funded_nodes:
                if node_id not in pending:
                    continue

                self.rpc.call(self.config.wallet_rpc_port, "open_wallet", {
                    "filename": wallet_name,
                    "password": "dummy"
                })
                self.rpc.call(self.config.wallet_rpc_port, "refresh")
                res = self.rpc.call(self.config.wallet_rpc_port, "get_balance")

                if res and "result" in res:
                    unlocked = res["result"]["unlocked_balance"]
                    if unlocked >= self.config.staking_requirement:
                        self.logger.debug(f"{wallet_name} unlocked: {unlocked:,}")
                    else:
                        still_pending.add(node_id)

            pending = still_pending

            if not pending:
                self.logger.info("All wallets unlocked!")
                return True

            if attempt % 10 == 0:
                self.logger.info(f"Still waiting for {len(pending)} wallets to unlock...")

            time.sleep(2)

        self.logger.warning(f"Timeout: {len(pending)} wallets still locked")
        return len(pending) == 0

    def _register_funded_node(self, node_id, wallet_name, sn_address):
        """Register a node that has already been funded and unlocked"""
        self.logger.info(f"Registering SN{node_id:02d}")

        # Open the SN wallet
        self.rpc.call(self.config.wallet_rpc_port, "open_wallet", {
            "filename": wallet_name,
            "password": "dummy"
        })
        self.rpc.call(self.config.wallet_rpc_port, "refresh")

        # Verify funds are unlocked
        res = self.rpc.call(self.config.wallet_rpc_port, "get_balance")
        if res and "result" in res:
            unlocked = res["result"]["unlocked_balance"]
            if unlocked < self.config.staking_requirement:
                self.logger.error(f"SN{node_id:02d} has insufficient unlocked balance: {unlocked:,}")
                return False

        # Get registration command from the SN daemon
        reg_cmd = self._get_registration_cmd(node_id, sn_address)
        if not reg_cmd:
            return False

        # Execute registration
        return self._execute_registration(reg_cmd)

    def _get_registration_cmd(self, node_id, sn_address):
        """Asks the Service Node Daemon for the registration string"""
        STAKING_PORTIONS = 18446744073709551612

        params = {
            "operator_cut": "10.0",  # Equilibria Horizon: Max operator fee is 10%
            "contributor_addresses": [sn_address],
            "contributor_amounts": [STAKING_PORTIONS],
            "staking_requirement": self.config.staking_requirement
        }

        # Each service node has a unique loopback IP
        sn_ip = f"127.0.0.{node_id}"
        sn_rpc_port = 18091 + (node_id - 1) * 2
        res = self.rpc.call_host(sn_ip, sn_rpc_port, "get_service_node_registration_cmd", params)

        if res and "result" in res:
            return res["result"]["registration_cmd"]

        self.logger.error(f"Failed to get registration command from {sn_ip}:{sn_rpc_port}")
        return None

    def _execute_registration(self, cmd):
        """Submits the registration command to the wallet"""
        self.logger.debug("Submitting registration transaction")
        res = self.rpc.call(self.config.wallet_rpc_port, "register_service_node", {
            "register_service_node_str": cmd
        })

        if res and "result" in res:
            tx_hash = res["result"]["tx_hash"]
            self.logger.info(f"Registration successful. Tx: {tx_hash[:16]}...")
            return True

        self.logger.error(f"Registration failed: {res}")
        return False

    # Keep the old sequential method for backwards compatibility
    def register_node(self, node_id):
        """Orchestrates the full registration flow for a single node (sequential method)"""
        self.logger.info(f"Starting automation for Service Node {node_id:02d}")

        sn_wallet_name = f"sn{node_id:02d}"
        sn_address = self._create_sn_wallet(sn_wallet_name)
        if not sn_address:
            return False

        self.logger.info(f"Funding {sn_wallet_name}")
        if not self._fund_wallet_sequential(sn_address, self.config.staking_requirement):
            return False

        self.rpc.call(self.config.wallet_rpc_port, "open_wallet", {
            "filename": sn_wallet_name,
            "password": "dummy"
        })

        self.logger.info(f"Waiting for funds to unlock in {sn_wallet_name}")
        if not self._wait_for_unlock(self.config.staking_requirement):
            return False

        reg_cmd = self._get_registration_cmd(node_id, sn_address)
        if not reg_cmd:
            return False

        return self._execute_registration(reg_cmd)

    def _fund_wallet_sequential(self, destination_address, amount):
        """Sequential funding with wait (old method)"""
        self.rpc.call(self.config.wallet_rpc_port, "open_wallet", {
            "filename": "genesis",
            "password": "dummy"
        })

        transfer_amount = amount + 1000000000

        self.logger.debug("Checking Genesis unlocked balance")
        if not self._wait_for_unlock(transfer_amount):
            self.logger.error("Genesis wallet never unlocked enough funds")
            return False

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
        max_retries = 60

        for i in range(max_retries):
            self.rpc.call(self.config.wallet_rpc_port, "refresh")
            res = self.rpc.call(self.config.wallet_rpc_port, "get_balance")

            if res and "result" in res:
                unlocked = res["result"]["unlocked_balance"]
                total = res["result"]["balance"]

                if unlocked >= required_amount:
                    self.logger.info(f"Funds unlocked! Balance: {unlocked}")
                    return True

                if i % 5 == 0:
                    self.logger.debug(f"Waiting for unlock. Current: {unlocked}/{required_amount} (Total: {total})")

            time.sleep(2)

        self.logger.error("Timed out waiting for funds to unlock")
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
            "--p2p-bind-ip=0.0.0.0",
            "--p2p-bind-port=18080",
            "--rpc-bind-port=18081",
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
            sn_ip = f"127.0.0.{i}"
            p2p_port = 18090 + (i-1) * 2
            rpc_port = 18091 + (i-1) * 2
            quorumnet_port = 38160

            cmd = [
                "docker", "run", "-dit", "--name", f"sn{i:02d}", "--network", "host",
                "-v", f"{os.getcwd()}/data/sn{i:02d}:/data", "equilibria-node",
                "--testnet", "--dev-allow-local-ips", "--service-node",
                f"--fixed-difficulty={self.config.difficulty}",
                "--data-dir=/data",
                f"--p2p-bind-ip={sn_ip}",
                f"--p2p-bind-port={p2p_port}",
                f"--rpc-bind-ip={sn_ip}",
                f"--rpc-bind-port={rpc_port}",
                "--add-priority-node=127.0.0.1:18080",
                f"--service-node-public-ip={sn_ip}",
                "--l2-provider=http://127.0.0.1:8545",
                f"--quorumnet-port={quorumnet_port}",
                "--log-level=3"
            ]

            if self.docker.run_command(cmd):
                self.logger.info(f"SN{i:02d} started (IP={sn_ip}, P2P={p2p_port}, RPC={rpc_port}, QNet={quorumnet_port})")
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

    def get_current_height(self) -> int:
        """Get the current blockchain height"""
        result = self.rpc.call(self.config.daemon_rpc_port, "get_info")
        if result and "result" in result:
            return result["result"].get("height", 0)
        return 0

    def get_service_node_state(self) -> Optional[ServiceNodeState]:
        """Get comprehensive service node state information"""
        result = self.rpc.call(self.config.daemon_rpc_port, "get_service_nodes")
        if not result or "result" not in result:
            self.logger.error("Failed to get service node state")
            return None

        sns = result["result"].get("service_node_states", [])
        current_height = self.get_current_height()

        nodes = []
        active_count = 0
        inactive_count = 0
        funded_count = 0

        for sn in sns:
            node = ServiceNodeInfo(
                pubkey=sn.get("service_node_pubkey", ""),
                pubkey_x25519=sn.get("pubkey_x25519", ""),
                active=sn.get("active", False),
                funded=sn.get("funded", False),
                state_height=sn.get("state_height", 0),
                last_uptime_proof=sn.get("last_uptime_proof", 0),
                public_ip=sn.get("public_ip", ""),
                quorumnet_port=sn.get("quorumnet_port", 0)
            )
            nodes.append(node)

            if node.active:
                active_count += 1
            else:
                inactive_count += 1

            if node.funded:
                funded_count += 1

        return ServiceNodeState(
            total_registered=len(nodes),
            active_count=active_count,
            inactive_count=inactive_count,
            funded_count=funded_count,
            nodes=nodes,
            current_height=current_height,
            pulse_min_required=self.config.pulse_min_service_nodes
        )

    def print_service_node_state(self):
        """Print the current service node state to the logger"""
        state = self.get_service_node_state()
        if state:
            for line in state.summary().split('\n'):
                self.logger.info(line)
        else:
            self.logger.error("Could not retrieve service node state")

    def get_active_service_node_count(self):
        """Get the number of active and funded service nodes"""
        state = self.get_service_node_state()
        if state:
            return state.active_count
        return 0

    def wait_for_active_service_nodes(self, required_count, timeout=600):
        """Wait until we have enough active service nodes"""
        self.logger.info(f"Waiting for {required_count} active service nodes")

        start_time = time.time()
        while time.time() - start_time < timeout:
            state = self.get_service_node_state()

            if state and state.active_count >= required_count:
                self.logger.info(f"Have {state.active_count} active service nodes (required: {required_count})")
                return True

            active = state.active_count if state else 0
            self.logger.debug(f"Active SNs: {active}/{required_count}, waiting...")
            time.sleep(5)

        self.logger.warning(f"Timeout waiting for active service nodes. Have {self.get_active_service_node_count()}, need {required_count}")
        return False

    def wait_for_pulse_ready_and_stop_mining(self):
        """Wait for HF16 height AND enough active SNs, then stop PoW mining"""
        self.logger.info(f"Waiting for Pulse readiness (HF16 at block {self.config.hf16_height}, need {self.config.pulse_min_service_nodes} active SNs)")

        # First wait for HF16 height
        self.wait_for_blocks(self.config.hf16_height)
        self.logger.info(f"HF16 height {self.config.hf16_height} reached")

        # Then wait for enough active service nodes
        if self.wait_for_active_service_nodes(self.config.pulse_min_service_nodes):
            self.logger.info("Pulse requirements met, stopping PoW mining")
            self.stop_mining()
            return True
        else:
            self.logger.warning("Not enough active SNs for Pulse, keeping PoW mining active")
            return False

    def create_dummy_transactions(self, count=20):
        """Create dummy transactions to populate output pool for ring signatures"""
        self.logger.info(f"Creating {count} dummy transactions to populate output pool")

        self.rpc.call(self.config.wallet_rpc_port, "open_wallet", {
            "filename": "genesis",
            "password": "dummy"
        })
        time.sleep(1)

        self.logger.info("Sweeping dust first")
        sweep_result = self.rpc.call(self.config.wallet_rpc_port, "sweep_dust", {
            "get_tx_keys": True
        })
        if sweep_result and "result" in sweep_result:
            self.logger.info("Dust swept successfully")
            time.sleep(5)

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
            amount = amounts[i % len(amounts)]

            result = self.rpc.call(self.config.wallet_rpc_port, "transfer", {
                "destinations": [{
                    "amount": amount,
                    "address": self.config.genesis_address
                }],
                "priority": 1,
                "ring_size": 2,
                "get_tx_key": True
            })

            if result and "result" in result:
                self.logger.debug(f"Dummy tx {i+1}/{count} created ({amount:,} atomic)")
            else:
                self.logger.warning(f"Dummy tx {i+1}/{count} failed: {result}")
                if "Not enough outputs" in str(result):
                    break

            time.sleep(1)

        self.logger.info("Dummy transaction creation complete")
        time.sleep(10)

        self.rpc.call(self.config.wallet_rpc_port, "refresh")
        time.sleep(3)

    def start_network(self):
        """Start the complete network"""
        self.logger.info("=" * 60)
        self.logger.info("Starting Equilibria Horizon Network")
        self.logger.info(f"Service nodes: {self.service_nodes}")
        self.logger.info(f"Regular nodes: {self.regular_nodes}")
        self.logger.info(f"Staking requirement: {self.config.staking_requirement:,} atomic ({self.config.staking_requirement // 1000000000:,} XEQ)")
        self.logger.info(f"HF16 (Pulse) activation: Block {self.config.hf16_height}")
        self.logger.info(f"Pulse min service nodes: {self.config.pulse_min_service_nodes}")
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

        # 5. Create Dummy Transactions
        self.create_dummy_transactions(15)

        # 6. Register Service Nodes (using parallel method)
        self.monitor.start()

        self.logger.info(f"Starting Parallel Registration for {self.service_nodes} nodes")
        registered_count = self.registrar.register_all_nodes_parallel(self.service_nodes)

        self.monitor.stop()

        # 7. Wait for Pulse readiness (HF16 + enough active SNs) and stop PoW mining
        if registered_count > 0:
            self.logger.info("Waiting for Pulse consensus to be ready")
            self.wait_for_pulse_ready_and_stop_mining()

        # Print final service node state
        self.print_service_node_state()

        self.logger.info("=" * 60)
        self.logger.info("Equilibria Horizon Network setup complete!")
        self.logger.info(f"Bootstrap: http://127.0.0.1:18081")
        self.logger.info(f"Wallet RPC: http://127.0.0.1:18084")
        if self.eth_node.node_directory:
            self.logger.info(f"Ethereum Node: http://127.0.0.1:{self.config.eth_node_port}")
        self.logger.info(f"Service nodes Registered: {registered_count}/{self.service_nodes}")
        self.logger.info(f"Active Service Nodes: {self.get_active_service_node_count()}")
        self.logger.info(f"Staking Requirement: {self.config.staking_requirement // 1000000000:,} XEQ")
        self.logger.info("=" * 60)

        return True
