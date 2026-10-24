# XEQM core 1.1.2 (what changed from 1.1.1)

**One mandatory correctness fix to the hf22 Pulse-recovery signed-fallback path, a version bump, and a CI hardening change. No other consensus, protocol, or API changes.**

This fix must be in the public build and on nodes before the rev1 activation (mainnet height **236920**, approx **2026-10-18**). Pre-rev1 the affected code path never runs, so 1.1.1 and 1.1.2 behave identically until then; at rev1 an unpatched node stalls (see below).

## Diff at a glance

Range: `core-v1.1.1` (a822fbe662) to `core-v1.1.2-rc1` (08faabd848) - 2 commits, 3 files.

```
 .github/workflows/build-core-binaries.yml | 44 +++++-----------   (CI only, not in the binary)
 CMakeLists.txt                            |  2 +-               (VERSION 1.1.1 -> 1.1.2)
 src/cryptonote_core/cryptonote_core.cpp   | 18 +++++------       (the fix)
```

Commits:
- `2bade3fe0` fix(hf22): signed-fallback reward stall + bump 1.1.2
- `08faabd84` ci(macos-intel): harden self-hosted runner job and make it non-blocking

## 1. The fix (the only runtime change)

File: `src/cryptonote_core/cryptonote_core.cpp`, function `try_produce_fallback_block()`.

Removed a single stray line: `b.reward = expected_reward;`

Why it was wrong: `create_block_template_internal` already sets `b.reward` to the correct batched coinbase (service_node_total + tx fees). The removed line overwrote that with the template's `expected_reward` out-param, which is the **tx-fee total only**, dropping `service_node_total`. The hf19+ reward-batching check (`validate_block_rewards`) requires `b.reward == miner_fee + service_node_total`, so every signed-fallback block failed with:

```
block reward for batching is incorrect: 0 != <SN reward>
```

Impact: at rev1 the first time Pulse fails over to the signed (PoW-free) fallback, the produced block would be rejected and the chain would stall - the opposite of what pulse-recovery is for. It is latent today because the signed-fallback producer path only runs under pulse-recovery (rev1).

Verification: reproduced empirically on a signer-from-genesis testnet - with the overwrite a fresh signer daemon stalls at height 1 ("0 != 8.25"); without it the chain climbs and block rewards are correct (fee + SN total). Guarded going forward by an integration test (`tests/integration/signer_fallback_reward.sh`, wired into the Ubuntu CI job).

Note: `b.reward` is serialized in the Oxen-10-interop block body, not the header, so it is not part of the hf22 block hash and never affected the fallback signature either way.

## 2. Version bump

`CMakeLists.txt`: `VERSION 1.1.1` -> `VERSION 1.1.2`.

## 3. CI hardening (does not affect the produced binary)

`.github/workflows/build-core-binaries.yml`: the self-hosted macOS Intel (x86_64) job was hardened so a runner flake cannot block the release - put Homebrew on PATH, clear stale formula locks, quiet Homebrew (`HOMEBREW_NO_INSTALL_CLEANUP` / `HOMEBREW_NO_ENV_HINTS`), and mark the job `continue-on-error: true`. The Intel assets attach in a separate job, so the core publish does not depend on them.

## For the public build

Tag the promoted release `core-v1.1.2` (final, no `-rc` / `-qa` / `-pre` suffix). `build-core-binaries.yml` triggers on `core-v*` tags and publishes; a plain `core-v1.1.2` tag publishes as a normal (non-prerelease) release. The tree is identical to the `core-v1.1.2-rc1` that is already running fleet-wide (same commit 08faabd848), so the final build is byte-for-byte the same source.
