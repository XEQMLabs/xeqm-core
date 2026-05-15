# Changelog

All notable changes to XEQM Core are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project follows semantic versioning where practical given the consensus-bound nature of the daemon.

## [1.0.2] — 2026-05-15

### Fixed

- **Fleet-wide cyclic decommissions** caused by a stale `x25519_to_pub` reverse map. Service nodes whose first uptime proof a daemon received after its own startup were unreachable for outbound OMQ timestamp tests; over hours/days of uptime this accumulated to ~88% test failure across the network and produced recurring decommissions with reason bit 16 (`timestamp_response_unreachable`). The legacy reverse map is now updated incrementally as proofs arrive, alongside the existing startup bulk-populate. Operators must update to v1.0.2 to receive this fix; the network self-heals as patched daemons replace unpatched ones. (`src/cryptonote_core/service_node_list.cpp`)

- **Linux release binaries are now actually portable.** Previous releases were dynamically linked against the build runner's Ubuntu 24.04 system libraries and segfaulted on most other distros. v1.0.2+ Linux release tarballs are built with `BUILD_STATIC_DEPS=ON` and depend only on glibc + libstdc++ at runtime. CI now enforces this with an `ldd` portability check that fails the build if any non-portable runtime dependency leaks in.

### Changed

- **Project version** bumped from `23.0.0` to `1.0.2` to align with the actual XEQM release scheme. The daemon banner now displays `XEQMLabs 'Ragnarok' (v1.0.2-...)`. Uptime proofs report version `{1, 0, 2}`. Pre-HF21 min-version checks are gated on hardforks mainnet does not reach, so this change does not trigger proof rejection. (`CMakeLists.txt`)

- **Binary naming standardized** on the `xeqm-` prefix. Operators using the canonical three (`xeqm-d`, `xeqm-wallet`, `xeqm-rpc`) are unaffected. Utility tools were renamed for consistency:

  | Old name | New name |
  | -------- | -------- |
  | `xeq-sn-keys` | `xeqm-sn-keys` |
  | `equilibria-bls` | `xeqm-bls` |
  | `equilibria-blockchain-import` | `xeqm-blockchain-import` |
  | `equilibria-blockchain-export` | `xeqm-blockchain-export` |
  | `equilibria-blockchain-usage` | `xeqm-blockchain-usage` |
  | `equilibria-blockchain-ancestry` | `xeqm-blockchain-ancestry` |
  | `equilibria-blockchain-depth` | `xeqm-blockchain-depth` |
  | `equilibria-blockchain-stats` | `xeqm-blockchain-stats` |
  | `equilibria-gen-trusted-multisig` | `xeqm-gen-trusted-multisig` |
  | `equilibria-utils-deserialize` | `xeqm-utils-deserialize` |
  | `equilibria-utils-object-sizes` | `xeqm-utils-object-sizes` |

  Anyone with scripts referencing the old names should update.

- **README.md** rewritten with current install paths, real resource links, accurate build instructions matching the CI pipeline, runtime configuration guidance, and a service-node-operator section.

- **CI Docker image** owner updated from the stale `equilibriahorizon` namespace to `xeqmlabs`. Image now publishes as `ghcr.io/xeqmlabs/equilibria-node:v1.0.2`.

### Added

- `SECURITY.md` with a private vulnerability disclosure process pointing to <security@xeqmlabs.com>.
- This `CHANGELOG.md`.

## Earlier releases

Prior versions were not maintained as a structured changelog. See the git history for individual changes.
