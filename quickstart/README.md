# XEQM Testnet Quickstart

Spin up XEQM testnet nodes, service nodes, or wallet services for local development and testing.

## Quick Start

### Prerequisites

- [Docker](https://docs.docker.com/get-docker/)
- [Docker Compose](https://docs.docker.com/compose/install/)

### Pull the image

```bash
docker pull ghcr.io/xeqmlabs/equilibria-node:v1.0.2
```

### Launch

The provided `docker-compose.yml` and `xeqm.conf` boot a testnet node ready for further configuration. From this directory:

```bash
docker compose up -d
docker compose logs -f
```

For a fleet of service nodes for development testing, see `equilibria_testnet.py` and edit the parameters at the top.

## Security Notes

- **Testnet only**: this stack is configured for testnet by default. Do not point it at mainnet without reviewing every flag.
- **Wallet backup**: always back up wallet files and keys before doing anything destructive. Loss of keys is loss of funds, even on testnet for committed registrations.
- **Public exposure**: only expose RPC ports beyond localhost if you understand the implications. Use `--restricted-rpc` and a reverse proxy if you need a public RPC.
- **Default config**: change any default credentials before relying on this for anything sensitive.
