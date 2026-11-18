# XEQM Quickstart

Run a XEQM node with Docker, or spin up a local test network for development.

## Quick Start

### Prerequisites

- [Docker](https://docs.docker.com/get-docker/)
- [Docker Compose](https://docs.docker.com/compose/install/)

### Pull the image

```bash
docker pull ghcr.io/xeqmlabs/xeqm-node:v1.1.4
```

### Launch

The provided `docker-compose.yml` and `xeqm.conf` run a mainnet node in the seed node layout: P2P on 9230, the public OMQ listener on 9233, and no public RPC. The compose file mounts the config from `/opt/xeqm/xeqm.conf` and keeps chain data in `/opt/xeqm/data`. Copy `xeqm.conf` there first, then from this directory:

```bash
docker compose up -d
docker compose logs -f
```

For a fleet of service nodes for development testing, see `equilibria_testnet.py` and edit the parameters at the top.

### Admin RPC

The admin RPC (port 9231) has no authentication and includes commands that stop the daemon. `xeqm.conf` binds it to `127.0.0.1` inside the container and the compose file does not publish it. Query it through the container:

```bash
docker exec xeqm-seed curl -s http://127.0.0.1:9231/json_rpc \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_info"}'
```

## Security Notes

- **Mainnet by default**: `xeqm.conf` connects to the mainnet seeds. Review every flag before you rely on it. `equilibria_testnet.py` is the tool for local test networks.
- **Wallet backup**: always back up wallet files and keys before doing anything destructive. Loss of keys is loss of funds, even on testnet for committed registrations.
- **Public exposure**: never expose the admin RPC (9231) beyond localhost. If other machines need RPC, add `rpc-public=IP:PORT`, which serves the public methods only.
- **Default config**: change any default credentials before relying on this for anything sensitive.
