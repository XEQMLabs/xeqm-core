# XEQM 1.1.4 release plan

As of 2026-10-24.

1.1.4 is a non-consensus software release built on 1.1.3. It carries the lokinet relay in a non-enforcing form, graduates the SQLite blockchain backend to production-ready with LMDB remaining the default, and introduces shared per-host chain storage so the service nodes on one host share a single chain instead of each keeping a full copy. It is not HF23.

## What 1.1.4 is

Built on 1.1.3, which already includes the review fixes from #58. Four headline contents:

1. **Review fixes (#58)**, already present in the 1.1.3 base.
2. **Lokinet, non-enforcing.** The daemon carries the dormant integration (`HAVE_LOKINET=false` on every network) plus the `lv` lokinet-version field in the uptime proof. The release bundle also ships the lokinet relay sidecar and its enable tooling, since the relay runs as a sidecar rather than inside the daemon. Enforcement stays off.
3. **Production-ready SQLite backend (#59)**, flag-gated, with LMDB as the default.
4. **Shared per-host chain storage (#62).** The service nodes on a host share one chain, maintained by a single writer process while the rest open it read-only, instead of each node keeping and syncing its own full copy. It ships on LMDB first, and if the writer fails a reader auto-promotes. See the definition of done below.

## What 1.1.4 is not

1.1.4 is not a hard fork. HF23 is a separate, future consensus release. It flips lokinet enforcement on (`HAVE_LOKINET=true`; the gate is a single check in `service_node_quorum_cop`, and its hard-fork-version floor is already satisfied, so no new fork height is needed for the flag itself) and depends on lokinet being installed and running fleet-wide. The lokinet soak that 1.1.4 enables is the prerequisite that makes HF23 safe to flip.

## Production-ready SQLite: definition of done

The SQLite backend ships today as experimental and opt-in. To call it production-ready in this release, close the following:

- Drop the "experimental" label the daemon logs on backend selection. **Done.** The selection log is now an info line, `Selecting the SQLite blockchain backend (XEQM_DB_BACKEND=sqlite)`, with no "experimental" wording and no warning level.
- Isolated-reader connection pool. **Done (#64).** This was a hard blocker, not a tuning nicety. On the single shared connection, a read colliding with the block-ingest write transaction threw `SQLITE_LOCKED`, which the daemon treats as fatal and shuts down. It was reproduced on a mainnet public RPC node under concurrent read load, which is why the SQLite public RPC nodes were rolled back to LMDB. The fix gives each non-writer thread its own read-only WAL connection and a real block read transaction, so reads take a snapshot instead of sharing the writer's connection (#64, landed on the #59 branch as commit `5aac6d1ca`). It is runtime-validated on a disposable node: 24 concurrent readers for 161s during continuous block ingest with zero wedges and no `SQLITE_LOCKED`, read p95 under 6ms up to 16 readers, and a from-scratch full-chain resync to tip with zero block mismatches against the LMDB reference. Re-enabling SQLite on the public RPC nodes now waits only on a fleet build cut from the #59 branch.
- Complete a service-node soak, not just public RPC nodes. A single canary service node first, then host by host.
- Live-verify the #58 fee-refuse path, which is source-confirmed today. **Done.** Verified over RPC on the signer-bootstrap testnet at hf22, the hard fork where before #58 the minimum-relay-fee check was skipped. An underpaid transfer (fee 0.000000001 XEQM) was rejected by the live daemon with `transaction fee is not enough: 0.000000001, minimum fee: 0.010067991` (`blockchain.cpp` `check_fee`) and `on_send_raw_tx ... Fee too low`, while the identical transfer at the real minimum (0.0103 XEQM) returned `status OK`. On a pre-#58 binary the underpaid tx would have been accepted at this hard fork, so the rejection is the #58 change confirmed at runtime.
- Merge #59 with a merge commit, to preserve the milestone history.
- Land installer SQLite support.

The default backend stays LMDB in 1.1.4, with SQLite opt-in via `XEQM_DB_BACKEND=sqlite`. Flipping the default to SQLite is a later, deliberate release decision once the fleet soak is broad.

## Shared per-host chain: definition of done

Today each service node on a host keeps its own full copy of the chain, so a host with N nodes stores, syncs, and page-caches the chain N times (missoula keeps roughly 18 copies). That duplication is the pressure behind most resource-driven decommissions. 1.1.4 replaces it with one chain per host: a single keyless writer maintains the chain and the host's service nodes open it read-only, holding only their own keys and proofs.

- Ships on LMDB first. LMDB already does cross-process one-writer, many-reader access on `main`, and because it is memory-mapped the readers share the same physical pages in the OS cache, which delivers the per-host memory and disk saving directly. The only backend gap is a daemon read-only open path. SQLite as the shared store is a follow-on, gated on the LMDB model proving out, because its true cross-process read-only open is still unbuilt.
- Writer failover is reader auto-promotion. Leadership is anchored to the chain write lock: a reader promotes only by acquiring the exclusive lock, never by voting that the writer looks dead. A renewing lease covers the alive-but-stalled writer, and the writer self-fences, stopping all writes the instant it cannot confirm its lease, so a recovered former writer never resumes alongside a new one. There is no preemption of a healthy writer, and the leadership token stays local to the host.
- Phase 0 is the go/no-go spike (#62, scope in `docs/shared-per-host-chain-phase-0.md`). It proves a read-only reader daemon opens the shared chain alongside a live writer with no ownership conflict, tracks the tip including across an LMDB mapsize growth, and serves the read paths a service node needs for quorum and Pulse duties. The reader and writer process split, the auto-promotion failover, and a multi-node host soak follow only if Phase 0 passes.

The correlated-failure trade-off is explicit. One chain and one writer per host is a shared failure domain, so at least one node keeps an independent current LMDB chain and the LMDB bootstrap keeps publishing, exactly as the SQLite rollout already requires.

## Soak and rollout

There is no rush to a public release. The build soaks on our own fleet for as long as it takes to shake out issues. SQLite rolls up the risk ladder, from public RPC nodes, to a canary service node, to host-by-host service nodes, with the LMDB rollback preserved on every host (the `lmdb/` directory is left intact during a switch).

Throughout, at least one node keeps building a current LMDB chain and the LMDB bootstrap keeps publishing, so a SQLite failure always has a fallback that needs no code patch.

## Build hygiene

The real 1.1.4 is a clean rebuild of 1.1.3 plus lokinet plus the production SQLite backend, with the fallback-signer rename (#61). The testnet signer-bootstrap harness is stripped from the release, since it is testnet-gated and not needed on mainnet, and the release is cut from a clean `v1.1.4` tag.

## Follow-on (post-1.1.4)

Shared per-host chain on SQLite. The 1.1.4 shared chain runs on LMDB; moving the shared store to SQLite is deferred until the LMDB model is proven and SQLite gains a true cross-process read-only open, which it lacks today. Tracked with #62.

A cross-host shared chain, or a client-server store such as Postgres for genuine multi-writer access, is a separate and later track, not a per-host optimization.
