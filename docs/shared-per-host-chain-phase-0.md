# Shared per-host chain storage, Phase 0 scope

Tracking issue: #62 (Shared per-host chain storage, one writer many readers).

Internal R&D scope. This document does not re-decide the architecture in #62; it turns that
design into a concrete, code-grounded first phase whose only job is to de-risk the reader /
writer process split before any production refactor is committed.

## The win, briefly

Every service node on a host keeps its own full chain today, so a host with N nodes stores,
syncs, and page-caches the chain N times (missoula keeps ~18 copies). That duplication is the
pressure behind most resource-driven decommissions. The target is one chain per host,
maintained by a single writer and read by the rest. See #62 for the full design, the shared
vs private state matrix, the SQLite-WAL choice, the supervised-singleton-writer recommendation,
and the correlated-failure trade-off.

## What Phase 0 is

A spike, not a refactor. Phase 0 answers one question with running code: can a second daemon
on a host, holding no chain-write responsibility, open an existing node's chain database and
track the tip as a separate writer process advances it, while doing so over distinct ports and
without an exclusive-ownership conflict? Everything else (production writer service, mempool
redesign, fleet rollout) is deliberately out of scope until that is proven.

Phase 0 is sequenced after the 1.1.4 SQLite backend proves out in single-node soak, per #62.
This document can be written and reviewed now; the spike work waits for that gate.

## Code map (where the real work is)

Anchors are on `main` unless noted. The pluggable SQLite *chain* backend and the
per-thread read-only reader fix (`a01fc7012`, issue #64) are on `deploy/114-sqlitefix`, not
`main`; `main`'s chain is LMDB only. Do not confuse the two SQLite classes: `BlockchainSQLite`
(`src/blockchain_db/sqlite/db_sqlite.*`, the reward-batching DB, on main) is separate from
`BlockchainSQLiteDB` (`src/blockchain_db/sqlite/blockchain_sqlite.*`, the real chain backend,
branch only).

- **Backend selection.** `new_db()` at `src/blockchain_db/blockchain_db.cpp:57` returns LMDB
  unconditionally on main; on the branch it reads `XEQM_DB_BACKEND=sqlite`. A real
  `--db-backend` flag is the intended replacement for the env gate.
- **Chain open path.** `core::init_blockchain_db()` at
  `src/cryptonote_core/cryptonote_core.cpp:968-1067` builds `db_flags` from `--db-sync-mode`
  and calls `db->open(folder, nettype, db_flags)` at `:1067`. The data dir is
  `<data-dir>/<db->get_db_name()>`. **The daemon never sets `DBF_RDONLY`** here; only the
  offline blockchain tools do. This flag builder is the core gap to fill.
- **LMDB read-only already works.** `BlockchainLMDB::open()` maps `DBF_RDONLY -> MDB_RDONLY`
  (`src/blockchain_db/lmdb/db_lmdb.cpp:1336`), guards every schema/migration write behind
  `!(mdb_flags & MDB_RDONLY)`, and `is_read_only()` reads the flag back (`:4576`). LMDB does
  not pass `MDB_NOLOCK`, so its native `lock.mdb` already supports one writer plus N readers
  across processes, and `mdb_env_set_maxreaders(threads+16)` (`:1328`) already leaves reader
  slots. The only backend-side risk is mapsize growth being writer-driven, so a reader must
  handle `MDB_MAP_RESIZED`.
- **SQLite read-only is same-process only, today.** `BlockchainSQLiteDB::open()` sets
  `m_read_only` but a genuine read-only open is explicitly deferred
  (`blockchain_sqlite.cpp:584-586`). The read-only `db::Database` mode added by `a01fc7012`
  still opens `OPEN_READWRITE` with `PRAGMA query_only=ON` and deliberately leaves WAL to the
  writer, so those reader connections still need write access to the directory to attach the
  `-wal`/`-shm` files. That is reader isolation inside one process, not a cross-process
  read-only open. A true `SQLITE_OPEN_READONLY` reader path is unbuilt.
- **No daemon-level lock.** There is no data-dir or PID lock for the daemon (`file_locker` is
  wallet-only). Single ownership today is de facto, enforced by P2P/RPC/ZMQ/quorumnet port
  binding per process, not by the DB.
- **The read-only pattern already exists to extend.** `is_read_only()` already gates work in
  `blockchain.cpp:876,933,956,6061`, `checkpoints.cpp:316`, `cryptonote_core.cpp:948`,
  `oxen_name_system.cpp:2162`, and the ONS DB already opens read-only vs read-write from
  `db->is_read_only()` (`cryptonote_core.cpp:754`, `oxen_name_system.cpp:645`). That is the
  propagation pattern a reader role follows.

## Phase 0 recommendation: prove it on LMDB first

#62 chooses SQLite WAL as the eventual shared store, and that still holds for the production
feature. But for de-risking the *process split*, LMDB is the lower-risk vehicle and it is
already on `main`:

- LMDB's cross-process one-writer / N-reader model exists and is honored end to end; only the
  daemon flag is missing.
- SQLite's true cross-process read-only open is an unbuilt prerequisite, so starting Phase 0 on
  SQLite means building that first and de-risking two things at once.

So Phase 0 proves the architecture on LMDB, and the SQLite true-read-only open becomes its own
scoped task that gates the eventual switch of the shared store to SQLite. Proving the split on
LMDB is not wasted: LMDB readers mmap the same file, so one host copy and a shared OS page cache
already deliver most of the memory and disk win even before SQLite.

## Phase 0 spike

1. Add a `--db-readonly` daemon flag (register near `BlockchainDB::init_options`,
   `cryptonote_core.cpp:401`) that ORs `DBF_RDONLY` into `db_flags` before `db->open(...)` at
   `cryptonote_core.cpp:1067`, and have the reader open the ONS and reward-batching DBs
   read-only as well (ONS already supports it; the reward DB needs the same treatment).
2. On one test host, run the normal daemon as writer, then start a second daemon with
   `--db-readonly`, the same `--data-dir`, distinct `--p2p-bind-port`/`--rpc`/`--zmq`/
   `--quorumnet-port` (the `utils/local-devnet/daemons.py:180-205` pattern), and no SN keys.
3. Confirm the reader opens without an ownership conflict, serves RPC reads, and tracks the tip
   as the writer advances, including across an LMDB mapsize growth (`MDB_MAP_RESIZED` handled).
4. Measure the resource delta against two independent daemons (disk, anon RSS, page cache, sync
   IO) to quantify the per-host saving.

## Open questions Phase 0 must resolve

- **Mempool ownership.** The mempool lives in the chain DB's txpool tables and `tx_pool` writes
  them unconditionally (`tx_pool.cpp:494,532-533,846,1038`). A reader must not write them.
  Recommended resolution to validate: the writer owns the mempool, readers read it read-only.
  This keeps the "writer owns all writes" invariant intact.
- **Pulse block production from a reader.** A reader SN selected as Pulse leader still produces
  and signs a block. It assembles the candidate from the read-only mempool and its own keys,
  then broadcasts over quorumnet; the writer ingests it through normal block handling and
  commits it. Phase 0 should confirm this path needs no reader write to the shared DB. Blocks
  are Pulse-produced and finalized by the quorum, so the reader never writes the chain directly.
- **Reader vs writer liveness.** A long-lived reader snapshot can hold back LMDB reclamation or
  SQLite WAL checkpointing; define snapshot lifetime limits and confirm a slow reader cannot
  stall the writer.
- **Writer process shape.** #62 recommends a keyless chain service as a supervised singleton.
  Phase 0's writer can be a normal daemon in writer mode; decide whether the production writer
  is a trimmed keyless build or the full daemon with SN duties disabled.
- **Correlated failure and fallback.** One DB and one writer is a shared failure domain. Keep at
  least one LMDB node current and the LMDB bootstrap published throughout, so a shared-chain
  stall has a fallback that needs no code patch.

## Explicitly out of scope for Phase 0

- The production keyless writer service and its systemd supervision.
- Writer failover of any kind (Phase 0 assumes a single supervised writer; peer election is a
  later phase if ever).
- The SQLite true cross-process read-only open (tracked as its own task; Phase 0 runs on LMDB).
- Any fleet rollout. First production target, when phases complete, is missoula.
- A cross-host shared chain or Postgres; that is a separate, later track.

## Go / no-go to Phase 1

Proceed to a production design only if Phase 0 shows: a read-only reader daemon opens the shared
chain alongside a live writer with no ownership conflict and no writer stall, tracks the tip
including across mapsize growth, serves the read paths an SN needs for quorum and Pulse duties,
and the measured per-host resource saving is large enough to justify adding the correlated
failure domain. If any of those fail, the fallback position is unchanged: keep one chain per SN
and pursue resource relief through the single-node SQLite backend and per-node tuning instead.
