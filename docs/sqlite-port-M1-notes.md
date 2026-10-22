# SQLite blockchain backend port — M1 notes

**Status:** M1 (compiling scaffold), 2026-10-19. Branch `feat/sqlite-light-node`.
Companion to [`light-node-port-scope.md`](light-node-port-scope.md). Upstream reference:
oxen-core [PR #1895](https://github.com/oxen-io/oxen-core/pull/1895), head
`a01ff45e5da047e674687365c88eac4c318ca3e5`.

## M1 goal and scope

Land a **compiling** scaffold of the combined SQLite `BlockchainDB` backend behind a
backend-selection switch, with **LMDB the default** and the new backend unreachable unless
explicitly selected. No schema reconciliation (that is M2) — the seams are stubbed.

What M1 delivers:

- A concrete `BlockchainDB` subclass (`cryptonote::BlockchainSQLiteDB`) that satisfies xeqm's
  full abstract interface, so it can be constructed and selected.
- Backend selection in `new_db()` defaulting to LMDB, picking SQLite only when
  `XEQM_DB_BACKEND=sqlite` is set.
- The new translation unit in the `blockchain_db` CMake target.

What M1 does **not** do: any real SQLite storage. Almost every method throws
`std::runtime_error("BlockchainSQLite: not implemented (M2): <method>")`. `open()` itself is a
stub, so selecting the backend fails loudly and immediately — it is deliberately not yet usable.

## Key interface divergence: upstream #1895 vs xeqm-core

The upstream `BlockchainSQLite` is written against a **different** abstract `BlockchainDB` than
xeqm's. The upstream PR reworked `blockchain_db.h` itself; xeqm's copy predates that rework. This
is why the port is a re-fit onto xeqm's contract, not a file copy. Concretely:

1. **Class-name collision.** Upstream's backend is `cryptonote::BlockchainSQLite`. But xeqm
   **already** has `cryptonote::BlockchainSQLite` in `sqlite/db_sqlite.h` — and it is a different
   thing: the rewards/batching database (`: public db::Database`), what upstream renamed to
   `rewards_db`. M1 keeps `db_sqlite.{h,cpp}` intact (per scope), so the new backend is named
   **`BlockchainSQLiteDB`** to avoid the clash. Reclaiming the plain name (rename rewards DB →
   `rewards_db`) is M2.

2. **SQLite wrapper library differs.** Upstream uses a newer `session::sqlite::{Database,
   Connection}` connection-pool API (`#include <sqlitedb/sqlite.hpp>`) plus `SQLite::Transaction`.
   xeqm has no `session::sqlite` at all — its wrapper is `db::Database` in `src/sqlitedb/`
   (`database.hpp`, the older oxen wrapper) used by the rewards DB. **The entire upstream data-access
   layer therefore does not compile against xeqm as-is.** Rebuilding it on xeqm's `db::Database`
   (or vendoring the newer wrapper) is core M2 work, not M1.

3. **Pruning.** xeqm's `BlockchainDB` keeps pure-virtual pruning methods
   (`prune_outputs`, `get_blockchain_pruning_seed`, `prune_blockchain`, `update_pruning`,
   `check_pruning`). Upstream #1895 **removed** them (dead pruning-stripe code). The new backend
   must still implement them for xeqm → stubbed (M2 can decide: real no-ops vs. the history-window
   model that replaces pruning upstream).

4. **History window (light nodes).** Upstream added non-pure virtuals to the base
   (`history()`, `history_start()`, `history_window_start()`, `served_history()`,
   `set_history()`, `trim_history()`, `start_history_catch_up()`) plus `--history`/`--db-backend`
   options, `db_backend` enum, `select_db_backend()`, `SQLITE_DB_FILENAME`, and the
   `active_write_transaction()` / `sqlite_database()` virtuals (to share the write txn with the
   rewards + ONS tables). **None of these exist in xeqm's base class yet.** The upstream
   `BlockchainSQLite` overrides them; here they are dropped from the port (nothing to override).
   Adding the light-node machinery to xeqm's base is M2/M3.

5. **Service-node state storage model differs.** xeqm's base uses the blob model:
   `set_service_node_data` / `get_service_node_data` / `clear_service_node_data`. Upstream replaced
   that with an **archive** model: `put_service_node_archive` / `get_service_node_archive` /
   `get_newest_full_service_node_archive` / `get_service_node_archive_heights` /
   `delete_service_node_archives` (+ a `delete_service_node_data(bool)`). The new backend implements
   xeqm's blob methods (stubbed); the archive methods are not part of xeqm's contract.

6. **Minor signature drift.**
   - `get_output_key(...)` bulk form: xeqm uses `epee::span<const uint64_t>`, upstream `std::span`.
   - `tx_exists` is two separate methods in xeqm (`(h)` and `(h, tx_id)`); upstream reworded docs.

### xeqm-specific state the stubs must respect in M2 (do NOT guess)

The scaffold throws rather than guess for anything tied to where xeqm diverged from oxen on
consensus/rewards, specifically:

- **Batching-v2 rewards** and the rewards schema (lives in the kept `db_sqlite.cpp` /
  `BlockchainSQLite` rewards DB) — `add_block` / `pop_block` / block accounting seams.
- **Fallback miner** and **rev1 signed fallback** (HF22/rev1): block add/validate paths and the
  miner-tx-optional handling differ from upstream.
- Anything reading or writing the rewards/accrued tables.

These are exactly the `// TODO(M2)` throw sites.

## What is real vs. stubbed in M1

**Implemented for real (touch no DB state):**
`BlockchainSQLiteDB(bool)`, `~BlockchainSQLiteDB()`, `get_db_name()` (returns `xeqm.db`),
`lock()` / `try_lock()` / `unlock()` (a `std::mutex`), `is_read_only()`.

**Everything else:** `// TODO(M2)` stub that throws
`std::runtime_error("BlockchainSQLite: not implemented (M2): <method>")` — including `open()`,
`close()`, `sync()`, `reset()`, all block/tx/output reads and writes, txpool, alt blocks,
checkpoints, output histogram/distribution, service-node data + proofs, pruning, batch/wtxn/rtxn
transaction management, and the private add/remove seams.

All 116 pure-virtual methods of xeqm's `BlockchainDB` are overridden (verified against the set
`BlockchainLMDB` implements), so the class is concrete. `fixup` and `pop_block(block&, txs)` are
left to the base-class implementations (they are virtual-with-default-impl, not pure).

## Files

- `src/blockchain_db/sqlite/blockchain_sqlite.h` / `.cpp` — the new backend (M1 scaffold).
- `src/blockchain_db/blockchain_db.cpp` — `new_db()` selector (LMDB default, env opt-in).
- `src/blockchain_db/CMakeLists.txt` — adds `sqlite/blockchain_sqlite.cpp` to the `blockchain_db`
  target. `db_sqlite.cpp` / `snapshots.cpp` left untouched and in the build.

Upstream reference copies are under `.port-ref-1895/` (git-excluded, not committed).

## Build

```
cmake --build build --target blockchain_db -j$(nproc)
```

LMDB remains `DEFAULT_DB_TYPE`; mainnet behaviour is unchanged (the SQLite path is only reached
when `XEQM_DB_BACKEND=sqlite` is explicitly set, and then throws at `open()`).
