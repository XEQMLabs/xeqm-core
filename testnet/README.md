# Equilibria Community Testnet Launcher

A simple, community-friendly tool for joining the Equilibria testnet. Launch testnet nodes, service nodes, or wallet services with a single command.

## 🚀 Quick Start

### Prerequisites

- **Docker**: [Install Docker](https://docs.docker.com/get-docker/)
- **Python 3.6+**
- **Available Ports**: 18080-18100 range

### Installation

1. **Download the files**:
   ```bash
   # Get both the library and community launcher
   wget https://your-repo.com/testnet_launcher.py
   wget https://your-repo.com/community_launcher.py

   # Make executable
   chmod +x community_launcher.py
   ```

2. **Install requirements**:
   ```bash
   pip3 install requests
   ```

3. **Verify Docker**:
   ```bash
   docker --version
   ```

## 📋 Commands

### Join Testnet (Regular Node)

```bash
python3 community_launcher.py testnet-node
```

**What it does:**
- Downloads the latest Equilibria Docker image
- Starts a testnet node that syncs with the network
- Connects to public bootstrap nodes automatically
- Provides RPC access at `http://127.0.0.1:18081`

**Custom ports:**
```bash
python3 community_launcher.py testnet-node --p2p-port 19080 --rpc-port 19081
```

### Run Service Node

```bash
python3 community_launcher.py service-node --public-ip YOUR_PUBLIC_IP
```

**What it does:**
- Starts a service node ready for registration
- Uses your public IP for network communication
- Provides RPC at `http://127.0.0.1:18091`
- **Note**: Registration requires 15,000 XEQ stake

**Get your public IP:**
```bash
python3 community_launcher.py service-node --public-ip $(curl -s ifconfig.me)
```

### Start Wallet Service

```bash
python3 community_launcher.py wallet-service
```

**What it does:**
- Starts wallet RPC service at `http://127.0.0.1:18084`
- Connects to local testnet node by default
- Default password: `testnet`
- Wallet files stored in `./wallets/`

**Connect to remote node:**
```bash
python3 community_launcher.py wallet-service --daemon-address remote.node.com:18081
```

### Monitor Network

```bash
python3 community_launcher.py monitor
```

**Real-time stats:**
- Current block height
- Network difficulty
- Peer connections
- Wallet balance (if wallet service running)

**Monitor with wallet:**
```bash
python3 community_launcher.py monitor --wallet-port 18084
```

### Full Development Network

```bash
python3 community_launcher.py full-network --service-nodes 5 --regular-nodes 2
```

**What it does:**
- Uses the original testnet launcher library
- Creates complete test environment with mining
- Automatically registers service nodes
- Perfect for development and testing

### Cleanup

```bash
python3 community_launcher.py cleanup
```

Removes all testnet containers and cleans up resources.

## 🔐 Service Node Registration

### Step 1: Setup Infrastructure

```bash
# Terminal 1: Start testnet node (for blockchain sync)
python3 community_launcher.py testnet-node

# Terminal 2: Start wallet service
python3 community_launcher.py wallet-service

# Terminal 3: Start service node
python3 community_launcher.py service-node --public-ip $(curl -s ifconfig.me)
```

### Step 2: Create Wallet

```bash
# Create wallet
curl -X POST http://127.0.0.1:18084/json_rpc \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc":"2.0",
    "id":"0",
    "method":"create_wallet",
    "params":{
      "filename":"my_service_node",
      "password":"secure_password",
      "language":"English"
    }
  }'

# Get wallet address
curl -X POST http://127.0.0.1:18084/json_rpc \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_address"}'
```

### Step 3: Fund Wallet

You need **15,000 XEQ minimum** for service node registration.

**Get testnet XEQ from:**
- Community faucet
- Mining on testnet
- Request from developers

### Step 4: Register Service Node

```bash
# Get registration command from your service node
curl -X POST http://127.0.0.1:18091/json_rpc \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc":"2.0",
    "id":"0",
    "method":"get_service_node_registration_cmd",
    "params":{
      "operator_cut":"100.0",
      "contributor_addresses":["YOUR_WALLET_ADDRESS"],
      "contributor_amounts":[15000000000000],
      "staking_requirement":15000000000000
    }
  }'

# Submit registration via wallet
curl -X POST http://127.0.0.1:18084/json_rpc \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc":"2.0",
    "id":"0",
    "method":"register_service_node",
    "params":{
      "register_service_node_str":"PASTE_REGISTRATION_COMMAND_HERE"
    }
  }'
```

## 💡 Use Cases

### For Community Members

**Simple testnet participation:**
```bash
python3 community_launcher.py testnet-node
```

**Monitor your node:**
```bash
python3 community_launcher.py monitor
```

### For Service Node Operators

**Start service node with monitoring:**
```bash
# Terminal 1
python3 community_launcher.py service-node --public-ip $(curl -s ifconfig.me)

# Terminal 2
python3 community_launcher.py wallet-service

# Terminal 3
python3 community_launcher.py monitor --wallet-port 18084
```

### For Developers

**Full test environment:**
```bash
python3 community_launcher.py full-network --service-nodes 3
```

**Custom wallet testing:**
```bash
python3 community_launcher.py wallet-service --rpc-port 18085
```

## 🔍 Troubleshooting

### Port Conflicts

```bash
# Check what's using a port
sudo lsof -i :18081

# Use different ports
python3 community_launcher.py testnet-node --rpc-port 19081
```

### Docker Issues

```bash
# Check Docker is running
docker ps

# Pull image manually
docker pull equilibria-node

# Check container logs
docker logs testnet-node
```

### Connection Problems

```bash
# Test connectivity to bootstrap nodes
telnet testnet1.equilibria.network 18080

# Check firewall settings
sudo ufw status
```

### Service Node Registration

**"Insufficient balance" error:**
- Ensure wallet has 15,000+ XEQ
- Wait for coins to unlock (30 blocks)
- Check balance: `curl -X POST http://127.0.0.1:18084/json_rpc -d '{"jsonrpc":"2.0","id":"0","method":"get_balance"}'`

**"Registration failed" error:**
- Verify service node is running and accessible
- Check public IP is correct and reachable
- Ensure all required ports are open

## 🌐 Network Information

### Default Ports

| Service | Port | Purpose |
|---------|------|---------|
| Testnet Node P2P | 18080 | Network communication |
| Testnet Node RPC | 18081 | API access |
| Service Node P2P | 18090 | Service node communication |
| Service Node RPC | 18091 | Service node API |
| Wallet RPC | 18084 | Wallet management |
| Quorumnet | 38160 | Service node consensus |

### Bootstrap Nodes

The launcher automatically connects to:
- `testnet1.equilibria.network:18080`
- `testnet2.equilibria.network:18080`
- `127.0.0.1:18080` (local fallback)

### Network Parameters

- **Staking Requirement**: 15,000 XEQ
- **Block Time**: ~2 minutes
- **Testnet Difficulty**: Fixed at 750
- **Service Node Registration**: Requires funded wallet

## 📊 Monitoring Commands

### Check Node Status

```bash
# Blockchain info
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_info"}'

# Service node status
curl -X POST http://127.0.0.1:18091/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_service_nodes"}'

# Wallet balance
curl -X POST http://127.0.0.1:18084/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_balance"}'
```

### View Logs

```bash
# Node logs
docker logs testnet-node -f

# Service node logs
docker logs service-node -f

# Wallet service logs
docker logs wallet-rpc -f
```

## 🛡️ Security Notes

- **Testnet Only**: This tool is for testnet use only
- **Default Passwords**: Change default passwords for any sensitive use
- **Public IPs**: Be cautious when exposing service nodes to internet
- **Wallet Backup**: Always backup wallet files and keys

## 🤝 Community

### Getting Help

- **Discord**: Join Equilibria community
- **GitHub**: Report issues and contribute
- **Documentation**: Visit official docs

### Contributing

- Test on different platforms
- Report bugs and issues
- Suggest improvements
- Help other community members

---

**Ready to join the testnet?** Start with:

```bash
python3 community_launcher.py testnet-node
```
