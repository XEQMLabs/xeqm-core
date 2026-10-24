# XEQM 1.1.4 release plan

As of 2026-10-24.

1.1.4 is a non-consensus software release built on 1.1.3. It carries the lokinet relay in a non-enforcing form and graduates the SQLite blockchain backend to production-ready, with LMDB remaining the default. It is not HF23.

## What 1.1.4 is

Built on 1.1.3, which already includes the review fixes from #58. Three headline contents:

1. **Review fixes (#58)**, already present in the 1.1.3 base.
2. **Lokinet, non-enforcing.** The daemon carries the dormant integration (`HAVE_LOKINET=false` on every network) plus the `lv` lokinet-version field in the uptime proof. The release bundle also ships the lokinet relay sidecar and its enable tooling, since the relay runs as a sidecar rather than inside the daemon. Enforcement stays off.
3. **Production-ready SQLite backend (#59)**, flag-gated, with LMDB as the default.

## What 1.1.4 is not

1.1.4 is not a hard fork. HF23 is a separate, future consensus release. It flips lokinet enforcement on (`HAVE_LOKINET=true`; the gate is a single check in `service_node_quorum_cop`, and its hard-fork-version floor is already satisfied, so no new fork height is needed for the flag itself) and depends on lokinet being installed and running fleet-wide. The lokinet soak that 1.1.4 enables is the prerequisite that makes HF23 safe to flip.

## Production-ready SQLite: definition of done

The SQLite backend ships today as experimental and opt-in. To call it production-ready in this release, close the following:

- Drop the "experimental" label the daemon logs on backend selection.
- Isolated-reader connection pool. **Done (#64).** This was a hard blocker, not a tuning nicety. On the single shared connection, a read colliding with the block-ingest write transaction threw `SQLITE_LOCKED`, which the daemon treats as fatal and shuts down. It was reproduced on a mainnet public RPC node under concurrent read load, which is why the SQLite public RPC nodes were rolled back to LMDB. The fix gives each non-writer thread its own read-only WAL connection and a real block read transaction, so reads take a snapshot instead of sharing the writer's connection (#64, landed on the #59 branch as commit `5aac6d1ca`). It is runtime-validated on a disposable node: 24 concurrent readers for 161s during continuous block ingest with zero wedges and no `SQLITE_LOCKED`, read p95 under 6ms up to 16 readers, and a from-scratch full-chain resync to tip with zero block mismatches against the LMDB reference. Re-enabling SQLite on the public RPC nodes now waits only on a fleet build cut from the #59 branch.
- Complete a service-node soak, not just public RPC nodes. A single canary service node first, then host by host.
- Live-verify the #58 fee-refuse path, which is source-confirmed today.
- Merge #59 with a merge commit, to preserve the milestone history.
- Land installer SQLite support.

The default backend stays LMDB in 1.1.4, with SQLite opt-in via `XEQM_DB_BACKEND=sqlite`. Flipping the default to SQLite is a later, deliberate release decision once the fleet soak is broad.

## Soak and rollout

There is no rush to a public release. The build soaks on our own fleet for as long as it takes to shake out issues. SQLite rolls up the risk ladder, from public RPC nodes, to a canary service node, to host-by-host service nodes, with the LMDB rollback preserved on every host (the `lmdb/` directory is left intact during a switch).

Throughout, at least one node keeps building a current LMDB chain and the LMDB bootstrap keeps publishing, so a SQLite failure always has a fallback that needs no code patch.

## Build hygiene

The real 1.1.4 is a clean rebuild of 1.1.3 plus lokinet plus the production SQLite backend, with the fallback-signer rename (#61). The testnet signer-bootstrap harness is stripped from the release, since it is testnet-gated and not needed on mainnet, and the release is cut from a clean `v1.1.4` tag.

## Follow-on (post-1.1.4)

Shared per-host chain storage. Today each service node on a host keeps its own full copy of the chain, so a host with N nodes stores and syncs the chain N times. The enhancement is one chain per host that the service nodes share, maintained by a single keyless writer and read by the rest, which cuts the duplicated disk, sync IO, and memory on multi-node hosts.

The failover model is the supervised singleton writer (one keyless, systemd-supervised chain-service as the sole write-lock holder), chosen over a peer-election scheme to rule out split-brain. This is R&D after the 1.1.4 SQLite single-node soak proves the backend, not part of the 1.1.4 ship. The design is tracked in #62.
