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
// M1 SCAFFOLD -- combined SQLite BlockchainDB backend (ported from oxen-core PR #1895).
//
// This is the milestone-1 scaffold of a single-file SQLite blockchain backend.  It is a
// concrete BlockchainDB subclass so that it can be selected at runtime, but almost every
// method is an explicit `TODO(M2)` stub that throws: the actual SQLite schema and the
// reconciliation with xeqm-core's diverged state (batching-v2 rewards, fallback miner,
// rev1 signed fallback, pruning, service-node archive vs blob storage) is deliberately
// deferred to milestone M2.  LMDB remains the default backend; this one is only reachable
// when XEQM_DB_BACKEND=sqlite is set explicitly (see new_db() in blockchain_db.cpp).
//
// NAMING: the upstream class is `cryptonote::BlockchainSQLite`, but that fully-qualified
// name is already taken in xeqm-core by the rewards/batching database in
// sqlite/db_sqlite.h (which is kept intact for M1).  To avoid the collision this class is
// named `BlockchainSQLiteDB`.  Reclaiming the plain name (by renaming the rewards DB to
// rewards_db) is an M2 task.
// =====================================================================================

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include "blockchain_db/blockchain_db.h"
#include "common/fs.h"

namespace cryptonote {

/// The combined SQLite backend's database file, directly inside the data directory.
inline constexpr std::string_view SQLITE_DB_BACKEND_FILENAME = "xeqm.db";

/// BlockchainDB backend storing the chain in a single SQLite database file.
///
/// M1: concrete-but-stubbed.  See the file header for what is and is not implemented.
class BlockchainSQLiteDB : public BlockchainDB {
  public:
    explicit BlockchainSQLiteDB(bool batch_transactions = true);
    ~BlockchainSQLiteDB() override;

    // --- BlockchainDB interface (all overrides; see .cpp for which are real vs M2 stubs) ---
    void add_block( const block& blk, size_t block_weight, uint64_t long_term_block_weight, const difficulty_type& cumulative_difficulty, const uint64_t& coins_generated, uint64_t num_rct_outs, const crypto::hash& blk_hash) override;
    void remove_block() override;
    uint64_t add_transaction_data( const crypto::hash& blk_hash, const std::pair<transaction, std::string>& tx, const crypto::hash& tx_hash, const crypto::hash& tx_prunable_hash) override;
    void remove_transaction_data(const crypto::hash& tx_hash, const transaction& tx) override;
    uint64_t add_output( const crypto::hash& tx_hash, const tx_out& tx_output, const uint64_t& local_index, const uint64_t unlock_time, const rct::key* commitment) override;
    void add_tx_amount_output_indices( const uint64_t tx_id, const std::vector<uint64_t>& amount_output_indices) override;
    void add_spent_key(const crypto::key_image& k_image) override;
    void remove_spent_key(const crypto::key_image& k_image) override;
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
    // Throws std::runtime_error("BlockchainSQLite: not implemented (M2): <method>").
    [[noreturn]] static void m2_stub(const char* method);

    mutable std::mutex m_lock;
    bool m_read_only = false;
    bool m_batch_transactions = true;
};

}  // namespace cryptonote
