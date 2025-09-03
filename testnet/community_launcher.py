#!/usr/bin/env python3
"""
Equilibria Testnet Community Launcher
Wrapper for the original testnet library with community-friendly interface
"""

import subprocess
import time
import json
import requests
import argparse
import os
import sys
from pathlib import Path
import threading

# Import the original library
from equilibria_network import (
    NetworkConfig, DockerManager, RPCClient, WalletManager,
    ServiceNodeRegistrar, NetworkMonitor, EquilibriaNetwork
)

class CommunityNode:
    """Community-friendly node wrapper"""

    def __init__(self):
        self.config = NetworkConfig()
        self.docker = DockerManager(self.config)
        self.rpc = RPCClient()
        self.container_name = None

    def check_requirements(self):
        """Check Docker availability"""
        try:
            result = subprocess.run(["docker", "--version"], capture_output=True, text=True)
            if result.returncode == 0:
                print(f"✅ Docker detected: {result.stdout.strip()}")
                return True
        except FileNotFoundError:
            pass

        print("❌ Docker not found. Please install Docker first.")
        print("   Visit: https://docs.docker.com/get-docker/")
        return False

    def pull_image(self):
        """Pull the latest image"""
        print("📥 Pulling Equilibria Docker image...")
        try:
            subprocess.run(["docker", "pull", "equilibria-node"], check=True)
            print("✅ Image pulled successfully")
            return True
        except subprocess.CalledProcessError:
            print("❌ Failed to pull image. Using local image if available.")
            return False

    def start_testnet_node(self, name="testnet-node", p2p_port=18080, rpc_port=18081, bootstrap_nodes=None):
        """Start a testnet node"""
        self.container_name = name
        data_dir = f"./data/{name}"
        Path(data_dir).mkdir(parents=True, exist_ok=True)

        # Cleanup existing
        self.docker.cleanup_all_containers([name])

        print(f"🚀 Starting testnet node: {name}")
        print(f"   P2P Port: {p2p_port}")
        print(f"   RPC Port: {rpc_port}")
        print(f"   Data Dir: {data_dir}")

        cmd = [
            "docker", "run", "-dit", "--name", name, "--network", "host",
            "-v", f"{os.path.abspath(data_dir)}:/data", "equilibria-node",
            "--testnet", "--dev-allow-local-ips", "--data-dir=/data",
            f"--p2p-bind-port={p2p_port}", f"--rpc-bind-port={rpc_port}",
            "--log-level=1"
        ]

        # Add bootstrap nodes
        bootstrap_list = bootstrap_nodes or self.config.public_bootstrap_nodes
        for node in bootstrap_list:
            cmd.extend(["--add-exclusive-node", node])

        return self.docker.run_command(cmd) is not None

    def start_service_node(self, name="service-node", p2p_port=18090, rpc_port=18091,
                          quorumnet_port=38160, public_ip="127.0.0.1", bootstrap_nodes=None):
        """Start a service node"""
        self.container_name = name
        data_dir = f"./data/{name}"
        Path(data_dir).mkdir(parents=True, exist_ok=True)

        # Cleanup existing
        self.docker.cleanup_all_containers([name])

        print(f"🔐 Starting service node: {name}")
        print(f"   P2P Port: {p2p_port}")
        print(f"   RPC Port: {rpc_port}")
        print(f"   Quorumnet Port: {quorumnet_port}")
        print(f"   Public IP: {public_ip}")

        cmd = [
            "docker", "run", "-dit", "--name", name, "--network", "host",
            "-v", f"{os.path.abspath(data_dir)}:/data", "equilibria-node",
            "--testnet", "--dev-allow-local-ips", "--service-node", "--data-dir=/data",
            f"--p2p-bind-port={p2p_port}", f"--rpc-bind-port={rpc_port}",
            f"--service-node-public-ip={public_ip}", f"--quorumnet-port={quorumnet_port}",
            "--l2-provider=http://dummy-provider", "--log-level=1"
        ]

        # Add bootstrap nodes
        bootstrap_list = bootstrap_nodes or self.config.public_bootstrap_nodes
        for node in bootstrap_list:
            cmd.extend(["--add-exclusive-node", node])

        return self.docker.run_command(cmd) is not None

    def start_wallet_service(self, rpc_port=18084, daemon_address="127.0.0.1:18081"):
        """Start wallet RPC service"""
        wallet_dir = "./wallets"
        Path(wallet_dir).mkdir(parents=True, exist_ok=True)

        # Cleanup existing
        self.docker.cleanup_all_containers(["wallet-rpc"])

        print(f"💳 Starting wallet RPC service")
        print(f"   RPC Port: {rpc_port}")
        print(f"   Daemon: {daemon_address}")

        cmd = [
            "docker", "run", "-d", "--name", "wallet-rpc", "--network", "host",
            "--entrypoint", "/usr/local/bin/xeq-wallet-rpc",
            "-v", f"{os.path.abspath(wallet_dir)}:/data", "equilibria-node",
            "--testnet", f"--rpc-bind-port={rpc_port}",
            f"--daemon-address={daemon_address}", "--disable-rpc-login",
            "--password=testnet", "--wallet-dir=/data", "--log-level=1"
        ]

        return self.docker.run_command(cmd) is not None

    def monitor_network(self, daemon_port=18081, wallet_port=None):
        """Monitor network status"""
        print("📊 Starting network monitor (Ctrl+C to stop)")

        try:
            while True:
                # Get daemon info
                daemon_info = self.rpc.call(daemon_port, "get_info")

                if daemon_info and "result" in daemon_info:
                    result = daemon_info["result"]
                    height = result.get("height", 0)
                    difficulty = result.get("difficulty", 0)
                    peers = result.get("outgoing_connections_count", 0)

                    status = f"📊 Block: {height:,} | Difficulty: {difficulty:,} | Peers: {peers}"

                    # Get wallet balance if wallet port provided
                    if wallet_port:
                        wallet_info = self.rpc.call(wallet_port, "get_balance")
                        if wallet_info and "result" in wallet_info:
                            balance = wallet_info["result"].get("balance", 0) / 1000000000
                            unlocked = wallet_info["result"].get("unlocked_balance", 0) / 1000000000
                            status += f" | Balance: {balance:.2f} XEQ | Unlocked: {unlocked:.2f} XEQ"

                    print(status)
                else:
                    print("❌ Cannot connect to daemon")

                time.sleep(10)

        except KeyboardInterrupt:
            print("\nStopping monitor...")

    def cleanup_containers(self):
        """Clean up all containers"""
        containers = ['testnet-node', 'service-node', 'wallet-rpc', 'bootstrap']
        # Add numbered containers
        for i in range(1, 21):
            containers.extend([f'sn{i:02d}', f'regular{i:02d}'])

        self.docker.cleanup_all_containers(containers)
        print("✅ Cleanup complete")

    def stop(self):
        """Stop current container"""
        if self.container_name:
            self.docker.cleanup_all_containers([self.container_name])

def main():
    """Main CLI interface"""
    parser = argparse.ArgumentParser(
        description="Equilibria Testnet Community Launcher",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Start a testnet node
  python3 community_launcher.py testnet-node --p2p-port 18080 --rpc-port 18081

  # Start a service node
  python3 community_launcher.py service-node --p2p-port 18090 --rpc-port 18091

  # Start wallet service
  python3 community_launcher.py wallet-service --rpc-port 18084

  # Monitor network
  python3 community_launcher.py monitor --daemon-port 18081

  # Launch full testnet (original functionality)
  python3 community_launcher.py full-network --service-nodes 5 --regular-nodes 2
        """
    )

    subparsers = parser.add_subparsers(dest='command', help='Commands')

    # Testnet node command
    testnet_parser = subparsers.add_parser('testnet-node', help='Start a testnet node')
    testnet_parser.add_argument('--name', default='testnet-node', help='Node name')
    testnet_parser.add_argument('--p2p-port', type=int, default=18080, help='P2P port')
    testnet_parser.add_argument('--rpc-port', type=int, default=18081, help='RPC port')
    testnet_parser.add_argument('--bootstrap-nodes', nargs='*', help='Bootstrap nodes')

    # Service node command
    service_parser = subparsers.add_parser('service-node', help='Start a service node')
    service_parser.add_argument('--name', default='service-node', help='Node name')
    service_parser.add_argument('--p2p-port', type=int, default=18090, help='P2P port')
    service_parser.add_argument('--rpc-port', type=int, default=18091, help='RPC port')
    service_parser.add_argument('--quorumnet-port', type=int, default=38160, help='Quorumnet port')
    service_parser.add_argument('--public-ip', default='127.0.0.1', help='Public IP address')
    service_parser.add_argument('--bootstrap-nodes', nargs='*', help='Bootstrap nodes')

    # Wallet service command
    wallet_parser = subparsers.add_parser('wallet-service', help='Start wallet RPC service')
    wallet_parser.add_argument('--rpc-port', type=int, default=18084, help='RPC port')
    wallet_parser.add_argument('--daemon-address', default='127.0.0.1:18081', help='Daemon address')

    # Monitor command
    monitor_parser = subparsers.add_parser('monitor', help='Monitor network status')
    monitor_parser.add_argument('--daemon-port', type=int, default=18081, help='Daemon RPC port')
    monitor_parser.add_argument('--wallet-port', type=int, help='Wallet RPC port')

    # Full network command (uses original library)
    full_parser = subparsers.add_parser('full-network', help='Launch full test network')
    full_parser.add_argument('--service-nodes', type=int, default=5, help='Number of service nodes')
    full_parser.add_argument('--regular-nodes', type=int, default=2, help='Number of regular nodes')

    # Cleanup command
    cleanup_parser = subparsers.add_parser('cleanup', help='Clean up all containers')

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        return

    # Initialize community node
    node = CommunityNode()

    # Check requirements
    if not node.check_requirements():
        sys.exit(1)

    # Pull latest image
    node.pull_image()

    try:
        if args.command == 'testnet-node':
            if node.start_testnet_node(args.name, args.p2p_port, args.rpc_port, args.bootstrap_nodes):
                print(f"\n🎉 Testnet node '{args.name}' is running!")
                print(f"RPC endpoint: http://127.0.0.1:{args.rpc_port}")
                print("Press Ctrl+C to stop...")
                try:
                    while True:
                        time.sleep(1)
                except KeyboardInterrupt:
                    pass
                finally:
                    node.stop()

        elif args.command == 'service-node':
            if node.start_service_node(args.name, args.p2p_port, args.rpc_port,
                                     args.quorumnet_port, args.public_ip, args.bootstrap_nodes):
                print(f"\n🎉 Service node '{args.name}' is running!")
                print(f"RPC endpoint: http://127.0.0.1:{args.rpc_port}")
                print("Note: Use wallet RPC to register as service node")
                print("Press Ctrl+C to stop...")
                try:
                    while True:
                        time.sleep(1)
                except KeyboardInterrupt:
                    pass
                finally:
                    node.stop()

        elif args.command == 'wallet-service':
            if node.start_wallet_service(args.rpc_port, args.daemon_address):
                print(f"\n🎉 Wallet RPC service is running!")
                print(f"RPC endpoint: http://127.0.0.1:{args.rpc_port}")
                print("Default password: testnet")
                print("Press Ctrl+C to stop...")
                try:
                    while True:
                        time.sleep(1)
                except KeyboardInterrupt:
                    pass
                finally:
                    node.docker.cleanup_all_containers(["wallet-rpc"])

        elif args.command == 'monitor':
            node.monitor_network(args.daemon_port, args.wallet_port)

        elif args.command == 'full-network':
            print("🚀 Launching full test network using original library...")
            network = EquilibriaNetwork(args.service_nodes, args.regular_nodes)
            try:
                network.start_network()
                print("\nPress Ctrl+C to stop network...")
                while True:
                    time.sleep(1)
            except KeyboardInterrupt:
                print("\nStopping network...")
            finally:
                network.cleanup_containers()

        elif args.command == 'cleanup':
            node.cleanup_containers()

    except KeyboardInterrupt:
        print("\nShutting down...")
    except Exception as e:
        print(f"❌ Error: {e}")
        sys.exit(1)

if __name__ == '__main__':
    main()
