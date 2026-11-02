# Shared per-host chain storage, Phase 1: the read-only reader

Tracking issue: #62. Follows the Phase 0 spike (`docs/shared-per-host-chain-phase-0.md`), which
proved on LMDB that a second daemon can open a live writer's chain read-only and track the tip, and
which identified that the hard part is not opening the database but keeping a reader's derived
consensus state current.

Internal R&D scope. LMDB first, as decided in the release plan. This document describes the Phase 1
reader: what it does, how refresh-to-tip works, what is implemented on `feat/shared-chain-reader`, and
what remains in Phase 1 before the keyless writer service and reader auto-promotion.

## The model

Each service node on a host runs its own daemon with its own full chain copy today, so a host with N
nodes stores, syncs, and page-caches the chain N times. Phase 1 lets a daemon open an existing chain
read-only with `--db-readonly`. One writer daemon maintains the chain; the others open it read-only and
hold only their own keys and proofs. Because LMDB is memory-mapped, the readers share the writer's
physical pages in the OS cache, so one host copy serves everyone.

A reader must never write the chain. The write surface splits cleanly:

- The chain data itself (blocks, outputs, key images) is written only by the writer through the normal
  block-add path. A reader never adds blocks; it maps the writer's committed ones.
- The daemon init path writes derived state in place (schema fixups, a service-node-list reset, the
  checkpoint-file load). Each of these is now guarded so a reader skips it. The writer owns them.
- Two background periodic tasks write: incremental pruning and the service-node-list state store. Both
  are skipped on a reader.

## The problem Phase 1 solves

Opening the chain read-only is the small part. The service node list is a derived projection: an
in-memory structure the daemon advances one block at a time through block-add hooks as it ingests
blocks. A read-only reader does not ingest blocks, so those hooks never fire, and its projection stays
frozen at whatever the last persisted blob held. Phase 0 measured this directly: with the chain tip at
the same height on writer and reader, the writer's service nodes showed a current reward height while
the reader's stayed roughly fifty blocks back and did not move.

The writer persists its service-node-list state only on a long interval and on a clean shutdown, and it
survives that long interval because after a restart it replays blocks forward from the last persisted
blob to the tip. That replay is the primitive a reader needs too. So the reader's staleness and the
writer's persistence cadence are one problem with one answer: a read-only replay-to-tip on the reader,
run once at startup and then continuously.

## Refresh-to-tip

`Blockchain::refresh_sn_list_readonly()` replays the writer's newly committed blocks, from the service
node list's current height up to the chain tip, advancing the in-memory SN set and quorums with no
write. For each height it reads the block and its transactions from the shared database and calls the
new `service_node_list::process_block_readonly()`.

`process_block_readonly()` locks the SN mutex and runs the same `process_block()` the live path uses,
followed by `rebuild_key_image_cache()`. It deliberately does not use `block_add()`, because `block_add`
does two things a reader cannot: it calls the reward-SQLite `add_block()` on every block, and it asserts
that the SN-list height equals the reward-SQLite height. A reader does not advance the reward SQLite, so
that invariant does not apply to it, and the add is a write it must not do.

The replay is write-free because `process_block()` advances the state purely in memory: it mutates the
in-memory `state_t` and its history and quorum buckets, reads the chain database through const
references only, and touches the reward SQLite solely at the hf21 and hf22-fixup boundary heights (a
one-time transition and a one-time fixup). A live chain is long past both, so a reader that starts above
them replays without any write.

One block-driven write does not fall under that boundary rule: when a block registers or recommissions a
service node, the state update persists that node's uptime proof through `proof_info::store`
(`set_service_node_proof`). Replaying such a block on a reader would write the shared DB, so
`proof_info::store` skips the persist on a read-only DB (the in-memory proof entry the caller already
updated is what the reader serves), and the periodic `cleanup_proofs` (which removes expired proofs)
returns early on a reader. The writer owns persisted proofs.

Two wiring points:

- Initial catch-up. `Blockchain::init` already skips the writing catch-up
  (`load_missing_blocks_into_oxen_subsystems`) on a read-only database. A read-only branch there calls
  `refresh_sn_list_readonly()` once, so the reader starts current with the tip rather than frozen at the
  persisted blob.
- Continuous follow. A one-second periodic task in `core::on_idle`, active only on a read-only reader,
  calls `refresh_sn_list_readonly()` to replay each new block the writer commits.

Read-snapshot renewal is automatic. LMDB read transactions are point-in-time snapshots, so a reader only
sees blocks another process committed after its read transaction is renewed. Each top-level database
read here (the tip height, each block fetch) starts a fresh read transaction that renews the per-thread
snapshot when it is not nested inside a batch, so the writer's new commits become visible on each tick.
`refresh_sn_list_readonly()` reads the tip once at the top and replays only blocks below it, all of them
already committed and immutable, so the scan is consistent.

## What is implemented

On `feat/shared-chain-reader` (off `main`):

- The `--db-readonly` flag and the `DBF_RDONLY` open path.
- Reader guards on every init-path and background write: the init fixup, both
  `service_node_list::reset` writes, the checkpoint-file load, and the pruning and SN-state-store
  periodic tasks.
- `service_node_list::process_block_readonly()` and `Blockchain::refresh_sn_list_readonly()`, wired for
  both the initial catch-up and the one-second follow.
- A `--shared-chain-writer` flag. A daemon started with it persists the recent service-node-list state
  once a minute (the cheap short-term store, separate from the long-term archive guard), so a reader on
  the host always loads from a recent blob. It is gated behind the flag, so ordinary fleet daemons keep
  their existing store cadence unchanged.
- ONS read-only. The reader shares the writer's `ons.db` opened `SQLITE_OPEN_READONLY`. Name resolution
  needs no per-block replay: lookups are plain SQL against the shared mapping rows and compute expiry
  from the caller-supplied chain height (the live tip), never from the in-memory `last_processed_height`,
  so a reader sees the writer's committed mappings as they land. `name_system_db` records a read-only
  flag at init and skips the two writes it would otherwise do on a reader: the destructor's settings
  save, and the init rebuild (DROP/recreate) path, which it refuses rather than attempts.
- Mempool read-only. The mempool lives in the shared chain DB's txpool tables and is owned by the
  writer. A reader reads it (its pool queries go to the shared DB and track the writer) but does not
  write it: the reader does not run the txpool relay (which updates each tx's relay bookkeeping),
  `remove_stuck_transactions` and `set_relayed`/`set_relayable` return early on a read-only DB, and
  `tx_memory_pool::init` loads the pool read-only without removing corrupt entries. The tx-acceptance
  and block-add/removal write paths are not reached by a reader that neither ingests txs over P2P nor
  adds blocks.
- Writer map growth (`MDB_MAP_RESIZED`). No new code is needed: when the writer grows the LMDB map,
  the reader's read-transaction paths (`block_rtxn_start` begin and renew) already route through the
  `lmdb_txn_begin` / `lmdb_txn_renew` wrappers that catch `MDB_MAP_RESIZED`, adopt the new size
  (`mdb_env_set_mapsize(env, 0)`), and retry. Validated on a build with an artificially tiny map: a
  read-only reader stayed correct and crash-free across eighty-plus writer map resizes over about
  thirteen minutes, tracking the writer's tip exactly and returning identical block hashes and an
  identical reported database size for data that had grown past the reader's initial map, with no
  read errors.

Runtime-validated on the signer-bootstrap testnet against a chain with three registered service nodes.
A read-only, network-isolated reader attached to the live writer's data directory and its service node
list tracked the writer exactly: at a shared tip the reader and writer reported identical per-node
reward heights, and after the tip advanced they stayed identical with the same top block hash. No write
errors occurred, confirming the guards. This is the state that Phase 0 had shown frozen. With
`--shared-chain-writer` on the writer, a reader attaching to a writer that had been running for minutes
without a restart loaded from a blob only about one cadence interval behind the tip and then caught the
small remainder, rather than loading an old blob or an empty state.

## Known limitation

If a reader's `load()` finds no persisted service-node-list blob at all, the list resets to just below
the first service-node hard fork, and refresh-to-tip would then try to replay from near genesis,
crossing the hf21 and hf22-fixup boundary heights where `process_block()` writes the reward SQLite; on a
read-only reward database that write fails. `--shared-chain-writer` makes this case not arise in
practice, since the writer always holds a recent post-fork blob for the reader to start from. A belt-and
-braces follow-up is to have the reader pass a null reward-SQLite handle at exactly those two heights so
even a from-genesis replay stays write-free.

## Remaining in Phase 1

- A reader acting as a Pulse leader. The reader's in-memory sorted pool is loaded at init and does not
  advance as the writer changes the pool, which is irrelevant to a pure reader but matters once a reader
  assembles block templates. That belongs with the reader-as-service-node work, alongside quorumnet and
  signing, not this read-only pass.

## After Phase 1

The keyless writer service and reader auto-promotion failover, as decided in the release plan:
leadership anchored to the chain write lock (acquire-to-promote, never a vote), a renewing lease for the
alive-but-stalled writer, and writer self-fencing, with no preemption of a healthy writer and the
leadership token kept local to the host. The shared-chain-on-SQLite store remains a separate follow-on
gated on the LMDB model proving out.
