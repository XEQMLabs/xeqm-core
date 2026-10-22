// Copyright (c) 2026, The XEQM Project
// Copyright (c) 2026, The Oxen Project
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#pragma once

// =====================================================================================
// Combined SQLite BlockchainDB backend (ported from oxen-core PR #1895).
//
// Milestone state (see docs/sqlite-port-M2-notes.md):
//   * M2a (this file): the data-access FOUNDATION + the CORE chain-storage path are real.
//     Lifecycle (open/close/sync/reset), transaction management, the write path
//     (add_block / add_transaction_data / add_output / add_spent_key / pop_block and the
//     remove_* seams), the read path (blocks, transactions, outputs, key images), txpool
//     and alt blocks are implemented against xeqm's `db::Database` (SQLiteCpp) wrapper.
//   * M2b (still TODO -- throws): anything tied to where xeqm diverged from oxen on
//     consensus/rewards -- rewards / batching-v2 / accrued-history, service-node data +
//     proofs + archive, fallback-miner / rev1 signed fallback, quorum-signed checkpoints,
//     plus pruning and histogram / output-distribution.  These deliberately throw
//     ("BlockchainSQLite: not implemented (M2b): <method>") rather than guess at the
//     diverged schema.
//
// NAMING: the upstream class is `cryptonote::BlockchainSQLite`, but that fully-qualified
// name is already taken in xeqm-core by the rewards/batching database in
// sqlite/db_sqlite.h (which is kept intact for now).  To avoid the collision this class is
// named `BlockchainSQLiteDB`.  Reclaiming the plain name (by renaming the rewards DB to
// rewards_db) is a later task.
//
// ACCESS LAYER: upstream #1895 uses a `session::sqlite` connection pool that xeqm-core does
// not have.  This port is rebuilt on xeqm's existing `db::Database` wrapper (src/sqlitedb,
// built on SQLiteCpp).  `db::Database` is a SINGLE shared connection (OPEN_FULLMUTEX, WAL,
// foreign_keys=ON) with a per-thread prepared-statement cache, so this backend uses one
// connection for both reads and writes rather than a pool.  See the M2 notes for the
// concurrency implications (and the connection-pool work left for a later milestone).
// =====================================================================================

#include <SQLiteCpp/SQLiteCpp.h>

#include <atomic>
#include <concepts>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <sqlitedb/database.hpp>
#include <string>
#include <string_view>
#include <thread>

#include "blockchain_db/blockchain_db.h"
#include "common/fs.h"

namespace cryptonote {

/// The combined SQLite backend's database file, directly inside the data directory.
inline constexpr std::string_view SQLITE_DB_BACKEND_FILENAME = "xeqm.db";

/// BlockchainDB backend storing the chain in a single SQLite database file.
class BlockchainSQLiteDB : public BlockchainDB {
  public:
    explicit BlockchainSQLiteDB(bool batch_transactions = true);
    ~BlockchainSQLiteDB() override;

    // --- BlockchainDB interface (see .cpp for which are real vs M2b stubs) ---
    void add_block( const block& blk, size_t block_weight, uint64_t long_term_block_weight, const difficulty_type& cumulative_difficulty, const uint64_t& coins_generated, uint64_t num_rct_outs, const crypto::hash& blk_hash) override;
    void remove_block() override;
    uint64_t add_transaction_data( const crypto::hash& blk_hash, const std::pair<transaction, std::string>& tx, const crypto::hash& tx_hash, const crypto::hash& tx_prunable_hash) override;
    void remove_transaction_data(const crypto::hash& tx_hash, const transaction& tx) override;
    uint64_t add_output( const crypto::hash& tx_hash, const tx_out& tx_output, const uint64_t& local_index, const uint64_t unlock_time, const rct::key* commitment) override;
    void add_tx_amount_output_indices( const uint64_t tx_id, const std::vector<uint64_t>& amount_output_indices) override;
    void add_spent_key(const crypto::key_image& k_image) override;
    void remove_spent_key(const crypto::key_image& k_image) override;
    void pop_block(block& blk, std::vector<transaction>& txs) override;
    void open( const fs::path& filename, cryptonote::network_type nettype, const int db_flags = 0) override;
    void close() override;
    void sync() override;
    void safesyncmode(const bool onoff) override;
    void reset() override;
    std::vector<fs::path> get_filenames() const override;
    bool remove_data_file(const fs::path& folder) const override;
    std::string get_db_name() const override;
    void lock() override;
    bool try_lock() override;
    void unlock() override;
    bool batch_start(uint64_t bytes_required = 0) override;
    void batch_stop() override;
    void batch_abort() override;
    void set_batch_transactions(bool) override;
    void block_wtxn_start() override;
    void block_wtxn_stop() override;
    void block_wtxn_abort() override;
    bool block_rtxn_start() const override;
    void block_rtxn_stop() const override;
    void block_rtxn_abort() const override;
    void update_block_checkpoint(checkpoint_t const& checkpoint) override;
    void remove_block_checkpoint(uint64_t height) override;
    bool get_block_checkpoint(uint64_t height, checkpoint_t& checkpoint) const override;
    bool get_top_checkpoint(checkpoint_t& checkpoint) const override;
    std::vector<checkpoint_t> get_checkpoints_range( uint64_t start, uint64_t end, size_t num_desired_checkpoints = GET_ALL_CHECKPOINTS) const override;
    bool block_exists(const crypto::hash& h, uint64_t* height = NULL) const override;
    std::string get_block_blob(const crypto::hash& h) const override;
    uint64_t get_block_height(const crypto::hash& h) const override;
    block_header get_block_header_from_height(uint64_t height) const override;
    std::string get_block_blob_from_height(uint64_t height) const override;
    block get_block_from_height(uint64_t height, size_t* size = nullptr) const override;
    uint64_t get_block_timestamp(const uint64_t& height) const override;
    std::vector<uint64_t> get_block_cumulative_rct_outputs( const std::vector<uint64_t>& heights) const override;
    uint64_t get_top_block_timestamp() const override;
    size_t get_block_weight(const uint64_t& height) const override;
    std::vector<uint64_t> get_block_weights(uint64_t start_height, size_t count) const override;
    difficulty_type get_block_cumulative_difficulty(const uint64_t& height) const override;
    difficulty_type get_block_difficulty(const uint64_t& height) const override;
    uint64_t get_block_already_generated_coins(const uint64_t& height) const override;
    uint64_t get_block_long_term_weight(const uint64_t& height) const override;
    std::vector<uint64_t> get_long_term_block_weights( uint64_t start_height, size_t count) const override;
    crypto::hash get_block_hash_from_height(const uint64_t& height) const override;
    std::vector<block> get_blocks_range(const uint64_t& h1, const uint64_t& h2) const override;
    std::vector<crypto::hash> get_hashes_range( const uint64_t& h1, const uint64_t& h2) const override;
    crypto::hash top_block_hash(uint64_t* block_height = NULL) const override;
    block get_top_block() const override;
    uint64_t height() const override;
    // --- light-node history retention (M4b) ---
    uint64_t history() const override { return m_history.load(); }
    uint64_t history_start() const override { return m_history_start.load(); }
    uint64_t history_window_start() const override;
    void set_history(uint64_t blocks) override;
    void trim_history() override;
    void start_history_catch_up() override;
    bool tx_exists(const crypto::hash& h) const override;
    bool tx_exists(const crypto::hash& h, uint64_t& tx_id) const override;
    uint64_t get_tx_unlock_time(const crypto::hash& h) const override;
    bool get_tx_blob(const crypto::hash& h, std::string& tx) const override;
    bool get_pruned_tx_blob(const crypto::hash& h, std::string& tx) const override;
    bool get_pruned_tx_blobs_from( const crypto::hash& h, size_t count, std::vector<std::string>& bd) const override;
    bool get_prunable_tx_blob(const crypto::hash& h, std::string& tx) const override;
    bool get_prunable_tx_hash( const crypto::hash& tx_hash, crypto::hash& prunable_hash) const override;
    uint64_t get_tx_count() const override;
    std::vector<transaction> get_tx_list(const std::vector<crypto::hash>& hlist) const override;
    std::vector<uint64_t> get_tx_block_heights( const std::vector<crypto::hash>& h) const override;
    uint64_t get_num_outputs(const uint64_t& amount) const override;
    output_data_t get_output_key( const uint64_t& amount, const uint64_t& index, bool include_commitmemt = true) const override;
    tx_out_index get_output_tx_and_index_from_global(const uint64_t& index) const override;
    tx_out_index get_output_tx_and_index( const uint64_t& amount, const uint64_t& index) const override;
    void get_output_tx_and_index( const uint64_t& amount, const std::vector<uint64_t>& offsets, std::vector<tx_out_index>& indices) const override;
    void get_output_key( const epee::span<const uint64_t>& amounts, const std::vector<uint64_t>& offsets, std::vector<output_data_t>& outputs, bool allow_partial = false) const override;
    bool can_thread_bulk_indices() const override;
    std::vector<std::vector<uint64_t>> get_tx_amount_output_indices( const uint64_t tx_id, size_t n_txes = 1) const override;
    bool has_key_image(const crypto::key_image& img) const override;
    void add_txpool_tx( const crypto::hash& txid, const std::string& blob, const txpool_tx_meta_t& details) override;
    void update_txpool_tx(const crypto::hash& txid, const txpool_tx_meta_t& details) override;
    uint64_t get_txpool_tx_count(bool include_unrelayed_txes = true) const override;
    bool txpool_has_tx(const crypto::hash& txid) const override;
    void remove_txpool_tx(const crypto::hash& txid) override;
    bool get_txpool_tx_meta(const crypto::hash& txid, txpool_tx_meta_t& meta) const override;
    bool get_txpool_tx_blob(const crypto::hash& txid, std::string& bd) const override;
    std::string get_txpool_tx_blob(const crypto::hash& txid) const override;
    void prune_outputs(uint64_t amount) override;
    uint32_t get_blockchain_pruning_seed() const override;
    bool prune_blockchain(uint32_t pruning_seed = 0) override;
    bool update_pruning() override;
    bool check_pruning() override;
    uint64_t get_max_block_size() override;
    void add_max_block_size(uint64_t sz) override;
    void add_alt_block( const crypto::hash& blkid, const cryptonote::alt_block_data_t& data, const std::string& blob, const std::string* checkpoint) override;
    bool get_alt_block( const crypto::hash& blkid, alt_block_data_t* data, std::string* blob, std::string* checkpoint) const override;
    void remove_alt_block(const crypto::hash& blkid) override;
    uint64_t get_alt_block_count() override;
    void drop_alt_blocks() override;
    bool for_all_txpool_txes( std::function<bool(const crypto::hash&, const txpool_tx_meta_t&, const std::string*)>, bool include_blob = false, bool include_unrelayed_txes = true) const override;
    bool for_all_key_images(std::function<bool(const crypto::key_image&)>) const override;
    bool for_blocks_range( const uint64_t& h1, const uint64_t& h2, std::function<bool(uint64_t, const crypto::hash&, const cryptonote::block&)>) const override;
    bool for_all_transactions( std::function<bool(const crypto::hash&, const cryptonote::transaction&)>, bool pruned) const override;
    bool for_all_outputs( std::function<bool( uint64_t amount, const crypto::hash& tx_hash, uint64_t height, size_t tx_idx)> f) const override;
    bool for_all_outputs( uint64_t amount, const std::function<bool(uint64_t height)>& f) const override;
    bool for_all_alt_blocks( std::function< bool(const crypto::hash& blkid, const alt_block_data_t& data, const std::string* block_blob, const std::string* checkpoint_blob)> f, bool include_blob = false) const override;
    std::map<uint64_t, std::tuple<uint64_t, uint64_t, uint64_t>> get_output_histogram( const std::vector<uint64_t>& amounts, bool unlocked, uint64_t recent_cutoff, uint64_t min_count) const override;
    bool get_output_distribution( uint64_t amount, uint64_t from_height, uint64_t to_height, std::vector<uint64_t>& distribution, uint64_t& base) const override;
    bool is_read_only() const override;
    uint64_t get_database_size() const override;
    void get_output_blacklist(std::vector<uint64_t>& blacklist) const override;
    void add_output_blacklist(std::vector<uint64_t> const& blacklist) override;
    void set_service_node_data(const std::string& data, bool long_term) override;
    bool get_service_node_data(std::string& data, bool long_term) const override;
    void clear_service_node_data() override;
    bool get_service_node_proof( const crypto::public_key& pubkey, service_nodes::proof_info& proof) const override;
    std::unordered_map<crypto::public_key, service_nodes::proof_info> get_all_service_node_proofs() const override;
    void set_service_node_proof( const crypto::public_key& pubkey, const service_nodes::proof_info& proof) override;
    bool remove_service_node_proof(const crypto::public_key& pubkey) override;

  private:
    // Throws std::runtime_error("BlockchainSQLite: not implemented (M2b): <method>").  Used for the
    // diverged rewards / service-node / consensus seams that this milestone does not implement.
    [[noreturn]] static void m2_stub(const char* method);

    void check_open() const;

    // The shared connection for a write.  Throws unless the calling thread holds the write
    // transaction (so that each block's rows -- transactions, outputs, key images and the block --
    // commit together and satisfy the deferred foreign keys to `blocks`).
    db::Database& write_conn();

    // Runs `f(db)` for a write made both inside and outside a write transaction (txpool, alt
    // blocks, max block size): inside the calling thread's write transaction if it holds one,
    // otherwise inside a transaction of its own, committed when `f` returns.
    template <std::invocable<db::Database&> F>
    void in_write_txn(F&& f) {
        check_open();
        if (m_writer.load() == std::this_thread::get_id()) {
            f(*m_db);
            return;
        }
        SQLite::Transaction tx{m_db->db, SQLite::TransactionBehavior::IMMEDIATE};
        f(*m_db);
        tx.commit();
    }

    void begin_write(bool batch);
    void end_write(bool commit);
    void require_writer(std::string_view action) const;
    void require_batch_writer() const;

    // The height below which the kept history ends, as of the current height (0 if all is kept, i.e.
    // a full node).  Blocks at [history_start, trim target) are dropped by the next trim.
    uint64_t history_trim_target() const;
    // Drops the block bodies and transactions outside the kept history, for heights
    // [m_history_start, to), and records the dropped input-less txs in trimmed_txes.  Must be called
    // by the thread holding the write transaction (it runs inline within it, so a trim and the block
    // add that triggered it commit together).
    void trim_history_to(uint64_t to);

    template <typename T>
    T get_block_column(uint64_t height, std::string_view column, std::string_view what) const;
    template <typename T>
    T get_stored_block(uint64_t height, size_t* size = nullptr) const;
    std::vector<uint64_t> get_block_weight_column(
            std::string_view column, uint64_t start_height, size_t count) const;

    std::unique_ptr<db::Database> m_db;
    fs::path m_filename;
    cryptonote::network_type m_nettype = cryptonote::network_type::UNDEFINED;
    int m_db_flags = 0;
    bool m_read_only = false;
    // NB: open/ready state lives in the public BlockchainDB::m_open base member (like BlockchainLMDB).
    // Do not redeclare it here: a shadowing member would be set by our methods but leave the base
    // m_open (read via BlockchainDB*: is_open(), core::init_blockchain_db) false, failing startup.

    mutable std::mutex m_lock;

    // The single write transaction, held only by the thread in m_writer.  Other threads only read
    // m_writer / m_batch_active.
    std::optional<SQLite::Transaction> m_write;
    std::atomic<std::thread::id> m_writer{};
    std::atomic<bool> m_batch_active{false};
    bool m_batch_transactions = true;

    // The stored `history` / `history_start` properties (see history() / history_start()).  Both 0
    // means a full node (keep everything), which is the default and a complete no-op.  Atomic so an
    // RPC thread reading served_history()/history_start() while the chain advances is safe.
    std::atomic<uint64_t> m_history{0};
    std::atomic<uint64_t> m_history_start{0};
};

}  // namespace cryptonote
