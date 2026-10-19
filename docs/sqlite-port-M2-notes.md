# SQLite blockchain backend port — M2 notes

**Status:** M2a (data-access foundation + core chain-storage path), 2026-10-19.
Branch `feat/sqlite-light-node`. Companion to [`sqlite-port-M1-notes.md`](sqlite-port-M1-notes.md).
Upstream reference: oxen-core [PR #1895](https://github.com/oxen-io/oxen-core/pull/1895).

## M2a goal and scope

Replace the M1 `TODO(M2)` throw-stubs for the **data-access foundation** and the **core
chain-storage path** with real implementations against xeqm's SQLite wrapper, while leaving
everything tied to xeqm's diverged rewards/consensus state as a clearly-marked `TODO(M2b)` throw.
No rewards / batching-v2 / accrued-history / service-node / checkpoint schema is created or guessed
at here.

## 1. Access approach: `db::Database` (SQLiteCpp), single connection

Upstream #1895 is written against `session::sqlite::{Database,Connection}` — a newer connection-pool
wrapper (`#include <sqlitedb/sqlite.hpp>`) that **xeqm-core does not have**. xeqm has two lower
layers available:

- the raw `sqlite3` C API, and
- `db::Database` in `src/sqlitedb/` (`database.hpp`/`.cpp`), the older oxen wrapper built on
  **SQLiteCpp**, already used by the rewards DB (`sqlite/db_sqlite.h`).

**Chosen: `db::Database`.** It is the lower-risk fit: it already gives us prepared-statement
caching (`prepared_exec`/`prepared_get`/`prepared_maybe_get`/`prepared_results`/`prepared_bind`),
blob binding/reading helpers (`blob_binder`, `bind_guts`, `blob_guts`), `multi_in_query`, and it
opens every database `WAL + synchronous=NORMAL + foreign_keys=ON` — exactly what this schema's
deferred `ON DELETE CASCADE` foreign keys need. Rebuilding on the raw C API, or vendoring the newer
`session::sqlite` pool, would both be more code and more risk for no M2a benefit.

The one structural difference from upstream: `db::Database` is a **single shared connection**
(`OPEN_FULLMUTEX`, with a per-thread prepared-statement cache), not a pool. So this backend uses one
connection for both reads and writes. Consequences, all handled here:

- **Writes.** There is a single `std::optional<SQLite::Transaction>` held by the writer thread
  (`m_writer`). The writer's own reads run inside that transaction, so (as with LMDB's batch) they
  see the block's not-yet-committed rows — which is exactly what `add_output` / `add_transaction_data`
  rely on when they compute `MAX(id)+1`, `MAX(height)+1`, etc. within a block/batch.
- **Reads.** Run in autocommit, serialised by `OPEN_FULLMUTEX`. `block_rtxn_start()` returns
  `false` (it starts no new snapshot) and `block_rtxn_stop/abort` are no-ops; the daemon treats a
  `false` return as "no read txn to stop", which is correct.
- **Concurrency caveat (deferred).** Because there is a single connection, a read issued from another
  thread *while the writer's transaction is open* is not an isolated snapshot. Under the core's
  `db.lock()` (held across block add/pop) this does not arise, and the single-threaded smoke path is
  unaffected. A true concurrent-reader model (a connection pool, mirroring upstream's
  `read_txn`/`write_txn`) is left for a later milestone, together with the light-node history window.

## 2. Schema created (core chain tables only)

`PRAGMA user_version = 1`. Tables (ported from upstream's `SCHEMA`, trimmed to the core set):

- `blocks` (+ `blocks_hash8`) — height PK, hash (+ virtual `hash8` prefix index), timestamp, weight,
  long-term weight, cumulative difficulty, coins generated, rct_outputs, block blob.
- `transactions` (+ `transactions_height`, `transactions_hash8`) — id PK, hash, height (deferred FK →
  blocks, cascade), type, miner flag, unlock_time, unprunable/prunable blobs, prunable_hash.
- `outputs` (+ unique `outputs_amount_index`, `outputs_height`, `outputs_blacklisted`, `outputs_tx`)
  — global output store keyed by `(amount, amount_index)`, with pubkey, commitment, owning tx/local
  index, and a `blacklisted` flag (RingCT SN-contribution outputs).
- `key_images` (+ `key_images_height`) — spent key image set, deferred FK → blocks, cascade.
- `properties` — key/value (holds `max_block_size`).
- `txpool` — mempool transactions + the full `txpool_tx_meta_t`.
- `alt_blocks` (+ `alt_blocks_height`) — alternative-chain blocks and their optional checkpoint blob.

**Deliberately NOT created** (left to M2b, to avoid guessing at xeqm's diverged schema): the
rewards/batching/accrued tables (these live in the kept `db_sqlite.cpp`), service-node state
(`sn_states`) and proofs (`sn_proofs`), quorum-signed `checkpoints`/`checkpoint_signatures`, and the
light-node history `trimmed_txes`.

**Hard-fork versions:** xeqm's `BlockchainDB` interface has **no** hard-fork-version accessor
methods (HF is derived from each block's `major_version`), and upstream #1895 likewise has no
hardforks table, so none is created. (The M2a brief listed "hard-fork versions" as a candidate core
table; there is nothing in the interface that would read or write it.)

## 3. What is REAL vs still TODO(M2b)

**REAL (implemented against `db::Database`):**

- Lifecycle: `open` (create schema on first open), `close`, `sync` (WAL checkpoint), `reset`,
  `safesyncmode` (no-op), `get_filenames`, `remove_data_file`, `get_database_size`, `get_db_name`,
  `lock`/`try_lock`/`unlock`, `is_read_only`.
- Transactions: `batch_start`/`batch_stop`/`batch_abort`, `set_batch_transactions`,
  `block_wtxn_start`/`stop`/`abort`, `block_rtxn_start`/`stop`/`abort`.
- Write path: the private `add_block`, `add_transaction_data`, `add_output`,
  `add_tx_amount_output_indices` (a no-op — the output row already records its tx + position),
  `add_spent_key`, `remove_block`, `remove_transaction_data`, `remove_spent_key`,
  `add_output_blacklist`, and a `pop_block(block&, txs&)` **override** (reads everything before the
  cascade delete; the base `BlockchainDB::pop_block` cannot be used because it removes the block row
  before reading its transactions). The public `add_block(..., txs)` and `add_transaction` use the
  base-class orchestration, which is pure chain storage with no rewards coupling.
- Block reads: `height`, `block_exists`, `get_block_height`, `top_block_hash`, `get_top_block`,
  `get_top_block_timestamp`, `get_block_from_height`, `get_block_header_from_height`,
  `get_block_blob`/`_from_height`, `get_block_hash_from_height`, `get_blocks_range`,
  `get_hashes_range`, timestamp/weight/long-term-weight/difficulty/cumulative-difficulty/
  generated-coins/cumulative-rct accessors, `get_block_weights`, `get_long_term_block_weights`.
- Tx reads: `tx_exists` (both), `get_tx_unlock_time`, `get_tx_blob`, `get_pruned_tx_blob`,
  `get_pruned_tx_blobs_from`, `get_prunable_tx_blob`, `get_prunable_tx_hash`, `get_tx_count`,
  `get_tx_list`, `get_tx_block_heights`.
- Output reads: `get_num_outputs`, `get_output_key` (single + bulk), `get_output_tx_and_index`
  (single/bulk/from-global), `get_tx_amount_output_indices`, `can_thread_bulk_indices`,
  `get_output_blacklist`.
- Key images: `has_key_image`, `for_all_key_images`.
- Iteration: `for_blocks_range`, `for_all_transactions`, `for_all_outputs` (both).
- Txpool: `add`/`update`/`remove`/`get_*`/`txpool_has_tx`/`get_txpool_tx_count`/`for_all_txpool_txes`.
- Alt blocks: `add`/`get`/`remove`/`get_alt_block_count`/`drop_alt_blocks`/`for_all_alt_blocks`.
- Max block size: `get_max_block_size`, `add_max_block_size`.

**TODO(M2b) — still throw** `"BlockchainSQLite: not implemented (M2b): <method>"`:

- Quorum-signed checkpoints: `update_block_checkpoint`, `remove_block_checkpoint`,
  `get_block_checkpoint`, `get_top_checkpoint`, `get_checkpoints_range`.
- Service nodes: `set`/`get`/`clear_service_node_data`, `get`/`set`/`remove_service_node_proof`,
  `get_all_service_node_proofs`.
- Pruning: `prune_outputs`, `get_blockchain_pruning_seed`, `prune_blockchain`, `update_pruning`,
  `check_pruning`.
- Histograms / output distribution: `get_output_histogram`, `get_output_distribution`.

Not reached by this port and left for later milestones: rewards / batching-v2 / accrued-history,
service-node archive storage, fallback-miner / rev1 signed-fallback block handling, and the
light-node history window (`history()`/`trim_history()`/etc., which are not part of xeqm's base
interface yet). `src/blockchain_db/sqlite/db_sqlite.{h,cpp}` (the rewards DB) is untouched; renaming
it to `rewards_db` and reclaiming the plain `BlockchainSQLite` name is a later step.

## 4. Behavioural notes

- Foreign keys from the per-block tables to `blocks` are `DEFERRABLE INITIALLY DEFERRED`: the base
  `add_block` adds a block's transactions/outputs/key-images before the block row, all inside one
  write transaction, so the keys are satisfied at commit. The cascade still fires immediately, so
  `pop_block` is a single `DELETE FROM blocks WHERE height = ?`.
- Binding: blobs are bound through `db::bind_guts` (trivially-copyable values, no-copy) and
  `db::blob_binder` (byte strings). Unlike upstream's `session::sqlite`, xeqm's `db::` layer does not
  treat a `std::optional<blob>` column read as SQL `NULL`, so nullable-blob reads (prunable parts,
  alt-block checkpoints, dropped block bodies) use an explicit `Column::isNull()` check.

## 5. Build

```
bash /home/svshearer/build-on-fernando.sh blockchain_db   # compile the backend
bash /home/svshearer/build-on-fernando.sh daemon          # confirm the daemon still links
```

LMDB remains `DEFAULT_DB_TYPE`; the SQLite path is reached only when `XEQM_DB_BACKEND=sqlite` is set
(see `new_db()` in `blockchain_db.cpp`). With this milestone that path can open a database, store a
block and read it back, but it is **not** yet a drop-in daemon backend: selecting it on a real node
will throw as soon as a checkpoint, service-node, pruning or histogram method is reached.
