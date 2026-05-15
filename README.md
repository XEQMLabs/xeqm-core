# XEQM Core

XEQM Core is the reference C++ daemon, wallet, and tooling for the XEQM service node network. The codebase descends from Monero → Loki/Oxen and inherits the service node consensus layer (Pulse, obligations quorums, OxenMQ messaging, checkpointing).

The three binaries shipped in every release are:

| Binary | Purpose |
| ------ | ------- |
| `xeqm-d` | Network daemon (full node, optionally a service node) |
| `xeqm-wallet` | CLI wallet |
| `xeqm-rpc` | Standalone wallet RPC server |

## Resources

**Project**
- Website: <https://xeqmlabs.com>
- Vision: <https://xeqmlabs.com/blog/xeqmlabs-vision>
- Block explorer: <https://explorer.xeqmlabs.com>

**Documentation**
- Documentation home: <https://xeqmlabs.gitbook.io/docs>
- Whitepaper: <https://github.com/XEQMLabs/whitepaper>
- Service nodes: <https://xeqmlabs.gitbook.io/docs/documentation/whitepaper/service-nodes-sn>
- Service node operator guide: <https://xeqmlabs.gitbook.io/docs/documentation/sn-operator-guide>
- Tokenomics: <https://xeqmlabs.gitbook.io/docs/documentation/whitepaper/tokenomics>

**Software**
- Wallet GUI releases: <https://github.com/XEQMLabs/XEQMLabs-GUI/releases>
- All XEQMLabs repositories: <https://github.com/XEQMLabs>

**Trade**
- NonLogs (XEQM/BTC): <https://nonlogs.io/trade/XEQM-BTC>
- NonLogs (XEQM/USDT): <https://nonlogs.io/trade/XEQM-USDT>
- NonKYC (XEQM/USDT): <https://nonkyc.io/market/XEQM_USDT>

**Community**
- Telegram: <https://t.me/XEQCommunity>

---

## Quick start (recommended)

The fastest way to run a full node is the published Docker image. It is built from this repository on every release tag and pushed to GitHub Container Registry.

```bash
docker pull ghcr.io/xeqmlabs/equilibria-node:v1.0.2
```

A minimal `docker-compose.yml` for a non-service-node full node:

```yaml
services:
  xeqm:
    image: ghcr.io/xeqmlabs/equilibria-node:v1.0.2
    container_name: xeqm
    restart: unless-stopped
    ports:
      - "9230:9230"   # P2P
      - "9231:9231"   # RPC
    volumes:
      - ./data:/data
    command:
      - --data-dir=/data
      - --p2p-bind-port=9230
      - --rpc-bind-port=9231
      - --confirm-external-bind
      - --log-level=*:warn
```

Bring it up with `docker compose up -d`. Tail logs with `docker logs -f xeqm`.

For a service node, add a `--service-node` flag, set `--service-node-public-ip`, and pick a `--quorumnet-port`. See [Running a service node](#running-a-service-node) below.

---

## Pre-built binaries

Each release tag (`core-v*`) publishes static binaries under [GitHub Releases](https://github.com/XEQMLabs/equilibria-core/releases) for:

- Ubuntu 24.04 x86_64
- Windows x86_64
- macOS ARM64

The Linux build is fully statically linked against its third-party dependencies (Boost, OpenSSL, libsodium, libzmq, libcurl, sqlite, gmp, zstd, etc.) and only requires glibc + libstdc++ at runtime. It runs unmodified on Ubuntu 22.04+, Debian 11+, and most other modern Linux distributions.

To verify on your machine after extracting the tarball:

```bash
ldd xeqm-d
# Should show only linux-vdso.so, libc.so, libstdc++.so, libgcc_s.so, libm.so,
# libpthread.so, libdl.so, librt.so, and the dynamic linker. Anything else
# (libssl, libboost, libsodium, libzmq, etc.) means the binary is not the
# release build and won't be portable.
```

The CI pipeline enforces this check; binaries shipped in releases are guaranteed to be portably linked.

---

## Building from source

You only need to build from source if you want to develop, audit, or run a custom build. Otherwise use the Docker image or pre-built binaries above.

### Linux (Debian / Ubuntu)

The build supports two modes. Pick one.

**Statically-linked (recommended for redistribution / matching CI):**

This builds Boost, OpenSSL, libsodium, libzmq, libcurl, etc. from source and links them into the binary. Same configuration the release pipeline uses. Slow first build (~30 min), produces portable binaries.

```bash
sudo apt update && sudo apt install -y \
  git build-essential cmake pkg-config ccache \
  libreadline-dev libhidapi-dev libusb-1.0-0-dev \
  libpgm-dev libsystemd-dev

git clone --recursive https://github.com/XEQMLabs/equilibria-core.git
cd equilibria-core

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_STATIC_DEPS=ON -DBoost_NO_BOOST_CMAKE=ON
make -j"$(nproc)"
```

**Dynamically-linked (faster, dev-only):**

Links against system libraries from apt. Faster builds, but the resulting binary only runs on your build host (and exact-version matching systems).

```bash
sudo apt update && sudo apt install -y \
  git build-essential cmake pkg-config \
  libssl-dev libzmq3-dev libsodium-dev libsqlite3-dev libcurl4-openssl-dev \
  libboost-all-dev libgmp-dev libzstd-dev libreadline-dev libhidapi-dev \
  libusb-1.0-0-dev libpgm-dev libsystemd-dev libunbound-dev

git clone --recursive https://github.com/XEQMLabs/equilibria-core.git
cd equilibria-core

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBoost_NO_BOOST_CMAKE=ON
make -j"$(nproc)"
```

Either way, binaries land in `build/bin/`.

### Windows

Builds use the MSYS2 MinGW64 toolchain. See `.github/workflows/build-core-binaries.yml` for the exact package list and CMake invocation. The CI builds windows binaries as fully static using `make release-static-win64`.

### macOS (Apple Silicon)

Requires Homebrew. The build uses pinned Boost 1.85 and bundles required Homebrew dylibs alongside the binary via `dylibbundler` so the resulting package is self-contained and runnable on any macOS 12+ ARM64 machine. See `.github/workflows/build-core-binaries.yml` for the full recipe.

---

## Running the daemon

Common configuration goes in `xeqm.conf` next to your data directory, or as command-line flags. Flags from the config file follow `key=value` syntax with no leading dashes.

### Minimum useful config (full node)

```ini
data-dir=/var/lib/xeqm
p2p-bind-ip=0.0.0.0
p2p-bind-port=9230
rpc-bind-ip=0.0.0.0
rpc-bind-port=9231
confirm-external-bind=1
log-level=*:warn
```

Run with:

```bash
xeqm-d --config-file=/etc/xeqm/xeqm.conf
```

To run as a systemd service, create a unit similar to:

```ini
[Unit]
Description=XEQM Daemon
After=network-online.target
Wants=network-online.target

[Service]
User=xeqm
Group=xeqm
ExecStart=/usr/local/bin/xeqm-d --config-file=/etc/xeqm/xeqm.conf --non-interactive
Restart=on-failure
RestartSec=10s
LimitNOFILE=65535

[Install]
WantedBy=multi-user.target
```

### Default network ports

| Network | P2P | RPC | Quorumnet |
| ------- | --- | --- | --------- |
| Mainnet | 9230 | 9231 | 9232 |

### Useful runtime commands

The daemon listens on a local OMQ admin socket and responds to JSON-RPC. A few commonly useful queries from outside the container:

```bash
# Daemon status and current chain height
curl -s http://127.0.0.1:9231/json_rpc -X POST -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_info"}' | jq .

# Service node status (only for SN daemons)
curl -s http://127.0.0.1:9231/json_rpc -X POST -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_service_node_status"}' | jq .

# Change log level on the running daemon (no restart)
curl -s http://127.0.0.1:9231/json_rpc -X POST -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"0","method":"set_log_level","params":{"categories":"*:warn,qnet:debug"}}'
```

---

## Running a service node

Service nodes earn rewards in exchange for participating in consensus (Pulse block production, checkpoint quorums, obligations quorums) and providing network reachability.

### Hardware

A modest VPS (2 vCPU, 4GB RAM, 80GB SSD, 1Gbps egress) is comfortable. CPU usage is low (single-digit percent) once synced; RAM use is dominated by the LMDB chain database.

### Network requirements

Three ports must be reachable from the public internet:

- P2P (default `9230`) — block / transaction propagation
- RPC (default `9231`) — daemon administration and JSON-RPC; restrict to localhost in production unless you know what you're doing
- Quorumnet (default `9232`) — service node ↔ service node messaging (Pulse, votes, timestamp tests)

A typical multi-node operator binds each daemon's quorumnet port to a unique value (9232, 9242, 9252, ...) so multiple SNs can share a single host.

### Economics

| Constant | Mainnet value |
| -------- | ------------- |
| Full stake | 200,000 XEQ |
| Minimum operator contribution | 100,000 XEQ (50% of stake) |
| Maximum operator fee | 10% (1,000 / 10,000 basis points) |
| Uptime proof frequency | 10 min |
| Uptime proof validity window | 21 min |

### Registration

Generate a registration string from the daemon, then submit a stake transaction with that string from your wallet. From the running daemon's interactive console (or via `xeqm-d <cmd>` for one-shot):

```
prepare_registration
```

Follow the prompts to set your operator fee and any reserved contributor allocations. The output is a registration command to run from your wallet:

```
xeqm-wallet register_service_node <registration string>
```

Once the transaction confirms (one block), the daemon will begin participating in pulse rounds, checkpoint quorums, and obligations quorums.

### Monitoring

The single most useful per-SN status check is:

```bash
PUBKEY=$(curl -s http://127.0.0.1:9231/json_rpc -X POST -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_service_node_status"}' \
  | jq -r '.result.service_node_state.service_node_pubkey')

curl -s http://127.0.0.1:9231/json_rpc -X POST -H "Content-Type: application/json" \
  -d "{\"jsonrpc\":\"2.0\",\"id\":\"0\",\"method\":\"get_service_nodes\",\"params\":{\"service_node_pubkeys\":[\"${PUBKEY}\"]}}" | jq .
```

Key fields to watch:

- `active`, `funded`, `payable` — should all be `true`
- `decommission_count` — should remain flat over time; rapid increase indicates trouble
- `earned_downtime_blocks` — credit toward future decom recovery; should grow, not shrink
- `last_uptime_proof` — should be recent (< proof frequency × 2)
- `pulse_votes`, `checkpoint_votes` — `voted` should approach 8, `missed` should be ~0

---

## Troubleshooting

### "Segfault on startup" / "GLIBC_2.x not found"

You're running a binary built against a newer system library set than your distro provides. Use the v1.0.2+ release tarballs, which are statically linked and portable, or pull the Docker image. Builds from before v1.0.2 binary releases were not properly statically linked and are known to fail this way on non-Ubuntu-24.04 hosts.

### "decommission_count keeps increasing"

This was a known bug fixed in v1.0.2 — `x25519_to_pub` reverse-map staleness over uptime caused outbound timestamp tests to fail with `TIMEOUT`, leading to cyclic decommissions. Update to v1.0.2+ and the issue will resolve.

### Inspecting OMQ-level behavior

To enable detailed OMQ proxy and quorumnet debug logging on a running daemon without restarting:

```bash
curl -s http://127.0.0.1:9231/json_rpc -X POST -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"0","method":"set_log_level","params":{"categories":"*:warn,oxenmq:debug,qnet:debug,service_nodes:debug,core:debug"}}'
```

To revert to defaults:

```bash
curl -s http://127.0.0.1:9231/json_rpc -X POST -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"0","method":"set_log_level","params":{"categories":"*:warn"}}'
```

### Stack traces from crashes

If the daemon has crashed and produced a coredump (`ulimit -c unlimited` enabled):

```bash
gdb /usr/local/bin/xeqm-d /path/to/core
(gdb) bt
(gdb) thread apply all bt
```

Or with systemd-coredump:

```bash
coredumpctl -1 gdb
```

Build with `-DSANITIZE=ON -DCMAKE_BUILD_TYPE=Debug` to get ASAN-instrumented binaries for memory-corruption analysis. Performance roughly halves.

---

## Development

### Tests

```bash
cd build
make release-test
```

The `core_tests` suite is slow (hours). Unit tests are fast.

### Local devnet

For developing against a local cluster of service nodes:

```bash
python3 utils/local-devnet/service_node_network.py \
  --eth-sn-contracts-dir ../eth-sn-contracts/ \
  --oxen-bin-dir ./build/bin
```

Note: requires a local Ethereum dev node (Foundry's `anvil` recommended) for full functionality, but most consensus-layer testing works without it.

### CI

CI is in `.github/workflows/`:

- `build-core-binaries.yml` — builds and publishes binary releases on `core-v*` tags
- `docker-publish.yml` — builds and publishes the docker image on push to `dev`

### Contributing

Pull requests welcome. Patches should be self-contained, logically separate from unrelated changes, and follow the surrounding code style. See [CONTRIBUTING.md](CONTRIBUTING.md) for details.

---

## Vulnerability disclosure

For sensitive security issues, please coordinate disclosure privately rather than opening a public issue. Contact information for the team is available via the repository's GitHub organization page.

---

## License

Distributed under the BSD 3-Clause License. See [LICENSE](LICENSE) for the full text.

Copyright notices:
- Copyright © 2024-present XEQMLabs
- Portions copyright © 2018-2024 The Oxen Project / The Loki Project
- Portions copyright © 2014-2024 The Monero Project
- Portions copyright © 2012-2013 The Cryptonote developers
