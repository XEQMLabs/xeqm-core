# Light-node + SQLite storage port — scope

**Status:** scope, 2026-10-16. Port of oxen-core
[PR #1895 "SQLite blockchain storage and light nodes"](https://github.com/oxen-io/oxen-core/pull/1895)
into xeqm-core.

**Decision:** land this **after HF22/rev1 (height 236920)** and **before HF23**, in the **Lokinet
release**, as opt-in / non-enforcing. HF23 only flips enforcement. The storage backend and light
nodes are non-consensus and wire-compatible, so they must NOT be bundled into the HF23 fork (same
discipline as introducing Lokinet non-enforcing first).

---

## What the upstream change is (three separable pieces)

1. **Combined SQLite backend** — one `oxen.db` for chain + service-node state + rewards + ONS,
   replacing LMDB. Purely local; no wire or consensus impact. Full node ~18GB LMDB -> ~16GB SQLite.
2. **Light nodes** — `--history=full|2w|3d|<blocks>`: keep only a recent window and still fully
   validate, sign, and serve wallets. A 2-week node is ~1.4GB. The history setting persists in the
   DB. Backward-compatible p2p: nodes advertise their depth and syncing nodes pick peers that can
   serve them, so mixed full/light networks sync correctly. **Not fork-gated.**
3. **Ancillary** — memory-usage optimizations (apply to LMDB too), min storage-server /
   session-router version bumps, removal of the dead pruning-stripe code, L2 proxy logging fix, a
   spurious BLS-key-mismatch warning fix.

## Why for XEQM

The driver is **fleet memory pressure**, not disk: SN hosts are hitting ~86% / OOM-kill risk. The
memory optimizations help even on LMDB, and a 2-week SQLite service node has a far smaller working
set. Disk savings (tens of GB -> low single-digit GB per SN) are a bonus.

- **Service nodes:** run `--history=2w` (upstream's suggestion). Big disk + memory win.
- **Self-backing wallet nodes:** a ~1.4GB validating node instead of a ~20GB full node (e.g. a
  wallet-backing node), provided it never needs to restore/scan from an old height locally.
- **Public / archive nodes:** keep **full (or long) history** — a wallet restoring from an old
  height needs blocks older than a 2-week window, which a light public node cannot serve.

## Starting state in xeqm-core

- Fork era ~oxen 11.x (eth/BLS), close to #1895's "compatible with 11.6" base — manageable gap.
- `src/blockchain_db/` has `lmdb/` plus a **narrow** `sqlite/` (the existing SN-rewards/batching DB
  only) — **not** the combined full-chain `oxen.db` backend. No `--history`, no `blockchain_sqlite`.
- XEQM has diverged in the files #1895 touches hardest: custom `network_config`
  (FALLBACK_MINER_PUBKEYS, batching-v2, rev1 signed-fallback), its own HF enum (hf20/21/hf22), and
  the rewards/batching schema.

## Effort and risks

- **Size:** ~11k LOC / 115 files. Largely mechanical where XEQM has not diverged.
- **Critical path — schema reconciliation:** the combined `oxen.db` schema must hold **XEQM's**
  divergent consensus state (batching-v2 rewards, fallback-miner data, rev1 signed-fallback), not
  oxen's. XEQM's existing `db_sqlite` rewards DB must fold into oxen's `rewards_db` /
  `accrued_history` cleanly. This is where the weeks go, and where a subtle bug corrupts SN
  rewards/state.
- **Migration / compat:** preserve "existing LMDB keeps working; new install = SQLite; pruned
  snapshot = light," so the fleet is never force-migrated.
- **Testing:** resync **parity** (LMDB vs SQLite must yield byte-identical chain + SN state +
  rewards), light-node validate/sign/serve, and mixed full/light sync. Leans on the multi-host
  testnet rig + integration tests, since the chaingen core_tests are partly broken on XEQM.

## Release plan

### Early quick win (can precede the Lokinet release)
- **Memory optimizations only.** They help existing LMDB nodes with no storage change — direct
  relief for the current 86%/OOM pressure. Lowest risk, highest immediate value. Ship as a patch
  release to the fleet.

### Release 1 — the Lokinet release (post-HF22, pre-HF23), opt-in / non-enforcing
- SQLite backend: new/resynced installs default SQLite; **existing fleet keeps LMDB** (no forced
  migration).
- Light-node `--history` + history advertisement + peer selection, opt-in; suggest `2w` for SNs.
- Publish a **2-week pruned snapshot** so operators adopt light mode by swapping data dirs.
- Lokinet / storage-server version **fields** in proofs, **non-enforcing**, aligned with the Lokinet
  rollout.
- Soak: watch resync parity, light-node participation, and memory/disk on volunteer hosts before
  broad adoption.

### HF23
- **Flip enforcement** of the min lokinet / storage-server version eligibility at the fork.
- The consensus fix-set already planned for HF23 (fallback-rounds 2->4, proof-window, etc.).
- **Not** the storage backend — it will already have soaked in Release 1.

## Open questions

- Does XEQM actually run a Session storage-server / session-router, or only lokinet? If only
  lokinet, the storage-server version bump is inert and only the lokinet version field matters for
  enforcement at HF23.
- Snapshot hosting + the resync-parity harness: reuse the existing multi-host testnet rig.
- Confirm the rev1 signed-fallback + batching-v2 state has a clean home in the combined schema
  before committing to the port (the critical-path risk above).
