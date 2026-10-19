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

// =====================================================================================
// M1 SCAFFOLD -- see blockchain_sqlite.h for scope.  Almost every method below is an
// explicit TODO(M2) stub that throws; the real SQLite schema + reconciliation with
// xeqm-core's diverged state is milestone M2.  The handful of methods implemented for
// real (constructor/destructor, get_db_name, lock/try_lock/unlock, is_read_only) touch no
// database state.
// =====================================================================================

#include "blockchain_sqlite.h"

#include <stdexcept>
#include <string>

namespace cryptonote {

void BlockchainSQLiteDB::m2_stub(const char* method) {
    // The marker string "BlockchainSQLite: not implemented (M2)" is deliberate; the method
    // name is appended to make the failure site obvious.
    throw std::runtime_error(std::string{"BlockchainSQLite: not implemented (M2): "} + method);
}

BlockchainSQLiteDB::BlockchainSQLiteDB(bool batch_transactions) :
        m_batch_transactions{batch_transactions} {}

BlockchainSQLiteDB::~BlockchainSQLiteDB() = default;

// M2_STUB routes an unimplemented method to the throwing helper above.
#define M2_STUB(method) m2_stub(method)

// The stub (and trivial) definitions below intentionally ignore most of their parameters.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

void BlockchainSQLiteDB::add_block( const block& blk, size_t block_weight, uint64_t long_term_block_weight, const difficulty_type& cumulative_difficulty, const uint64_t& coins_generated, uint64_t num_rct_outs, const crypto::hash& blk_hash) {
    M2_STUB("add_block");
}

void BlockchainSQLiteDB::remove_block() {
    M2_STUB("remove_block");
}

uint64_t BlockchainSQLiteDB::add_transaction_data( const crypto::hash& blk_hash, const std::pair<transaction, std::string>& tx, const crypto::hash& tx_hash, const crypto::hash& tx_prunable_hash) {
    M2_STUB("add_transaction_data");
}

void BlockchainSQLiteDB::remove_transaction_data(const crypto::hash& tx_hash, const transaction& tx) {
    M2_STUB("remove_transaction_data");
}

uint64_t BlockchainSQLiteDB::add_output( const crypto::hash& tx_hash, const tx_out& tx_output, const uint64_t& local_index, const uint64_t unlock_time, const rct::key* commitment) {
    M2_STUB("add_output");
}

void BlockchainSQLiteDB::add_tx_amount_output_indices( const uint64_t tx_id, const std::vector<uint64_t>& amount_output_indices) {
    M2_STUB("add_tx_amount_output_indices");
}

void BlockchainSQLiteDB::add_spent_key(const crypto::key_image& k_image) {
    M2_STUB("add_spent_key");
}

void BlockchainSQLiteDB::remove_spent_key(const crypto::key_image& k_image) {
    M2_STUB("remove_spent_key");
}

void BlockchainSQLiteDB::open( const fs::path& filename, cryptonote::network_type nettype, const int db_flags) {
    M2_STUB("open");
}

void BlockchainSQLiteDB::close() {
    M2_STUB("close");
}

void BlockchainSQLiteDB::sync() {
    M2_STUB("sync");
}

void BlockchainSQLiteDB::safesyncmode(const bool onoff) {
    M2_STUB("safesyncmode");
}

void BlockchainSQLiteDB::reset() {
    M2_STUB("reset");
}

std::vector<fs::path> BlockchainSQLiteDB::get_filenames() const {
    M2_STUB("get_filenames");
}

bool BlockchainSQLiteDB::remove_data_file(const fs::path& folder) const {
    M2_STUB("remove_data_file");
}

std::string BlockchainSQLiteDB::get_db_name() const {
    return std::string{SQLITE_DB_BACKEND_FILENAME};
}

void BlockchainSQLiteDB::lock() {
    m_lock.lock();
}

bool BlockchainSQLiteDB::try_lock() {
    return m_lock.try_lock();
}

void BlockchainSQLiteDB::unlock() {
    m_lock.unlock();
}

bool BlockchainSQLiteDB::batch_start(uint64_t bytes_required) {
    M2_STUB("batch_start");
}

void BlockchainSQLiteDB::batch_stop() {
    M2_STUB("batch_stop");
}

void BlockchainSQLiteDB::batch_abort() {
    M2_STUB("batch_abort");
}

void BlockchainSQLiteDB::set_batch_transactions(bool) {
    M2_STUB("set_batch_transactions");
}

void BlockchainSQLiteDB::block_wtxn_start() {
    M2_STUB("block_wtxn_start");
}

void BlockchainSQLiteDB::block_wtxn_stop() {
    M2_STUB("block_wtxn_stop");
}

void BlockchainSQLiteDB::block_wtxn_abort() {
    M2_STUB("block_wtxn_abort");
}

bool BlockchainSQLiteDB::block_rtxn_start() const {
    M2_STUB("block_rtxn_start");
}

void BlockchainSQLiteDB::block_rtxn_stop() const {
    M2_STUB("block_rtxn_stop");
}

void BlockchainSQLiteDB::block_rtxn_abort() const {
    M2_STUB("block_rtxn_abort");
}

void BlockchainSQLiteDB::update_block_checkpoint(checkpoint_t const& checkpoint) {
    M2_STUB("update_block_checkpoint");
}

void BlockchainSQLiteDB::remove_block_checkpoint(uint64_t height) {
    M2_STUB("remove_block_checkpoint");
}

bool BlockchainSQLiteDB::get_block_checkpoint(uint64_t height, checkpoint_t& checkpoint) const {
    M2_STUB("get_block_checkpoint");
}

bool BlockchainSQLiteDB::get_top_checkpoint(checkpoint_t& checkpoint) const {
    M2_STUB("get_top_checkpoint");
}

std::vector<checkpoint_t> BlockchainSQLiteDB::get_checkpoints_range( uint64_t start, uint64_t end, size_t num_desired_checkpoints) const {
    M2_STUB("get_checkpoints_range");
}

bool BlockchainSQLiteDB::block_exists(const crypto::hash& h, uint64_t* height) const {
    M2_STUB("block_exists");
}

std::string BlockchainSQLiteDB::get_block_blob(const crypto::hash& h) const {
    M2_STUB("get_block_blob");
}

uint64_t BlockchainSQLiteDB::get_block_height(const crypto::hash& h) const {
    M2_STUB("get_block_height");
}

block_header BlockchainSQLiteDB::get_block_header_from_height(uint64_t height) const {
    M2_STUB("get_block_header_from_height");
}

std::string BlockchainSQLiteDB::get_block_blob_from_height(uint64_t height) const {
    M2_STUB("get_block_blob_from_height");
}

block BlockchainSQLiteDB::get_block_from_height(uint64_t height, size_t* size) const {
    M2_STUB("get_block_from_height");
}

uint64_t BlockchainSQLiteDB::get_block_timestamp(const uint64_t& height) const {
    M2_STUB("get_block_timestamp");
}

std::vector<uint64_t> BlockchainSQLiteDB::get_block_cumulative_rct_outputs( const std::vector<uint64_t>& heights) const {
    M2_STUB("get_block_cumulative_rct_outputs");
}

uint64_t BlockchainSQLiteDB::get_top_block_timestamp() const {
    M2_STUB("get_top_block_timestamp");
}

size_t BlockchainSQLiteDB::get_block_weight(const uint64_t& height) const {
    M2_STUB("get_block_weight");
}

std::vector<uint64_t> BlockchainSQLiteDB::get_block_weights(uint64_t start_height, size_t count) const {
    M2_STUB("get_block_weights");
}

difficulty_type BlockchainSQLiteDB::get_block_cumulative_difficulty(const uint64_t& height) const {
    M2_STUB("get_block_cumulative_difficulty");
}

difficulty_type BlockchainSQLiteDB::get_block_difficulty(const uint64_t& height) const {
    M2_STUB("get_block_difficulty");
}

uint64_t BlockchainSQLiteDB::get_block_already_generated_coins(const uint64_t& height) const {
    M2_STUB("get_block_already_generated_coins");
}

uint64_t BlockchainSQLiteDB::get_block_long_term_weight(const uint64_t& height) const {
    M2_STUB("get_block_long_term_weight");
}

std::vector<uint64_t> BlockchainSQLiteDB::get_long_term_block_weights( uint64_t start_height, size_t count) const {
    M2_STUB("get_long_term_block_weights");
}

crypto::hash BlockchainSQLiteDB::get_block_hash_from_height(const uint64_t& height) const {
    M2_STUB("get_block_hash_from_height");
}

std::vector<block> BlockchainSQLiteDB::get_blocks_range(const uint64_t& h1, const uint64_t& h2) const {
    M2_STUB("get_blocks_range");
}

std::vector<crypto::hash> BlockchainSQLiteDB::get_hashes_range( const uint64_t& h1, const uint64_t& h2) const {
    M2_STUB("get_hashes_range");
}

crypto::hash BlockchainSQLiteDB::top_block_hash(uint64_t* block_height) const {
    M2_STUB("top_block_hash");
}

block BlockchainSQLiteDB::get_top_block() const {
    M2_STUB("get_top_block");
}

uint64_t BlockchainSQLiteDB::height() const {
    M2_STUB("height");
}

bool BlockchainSQLiteDB::tx_exists(const crypto::hash& h) const {
    M2_STUB("tx_exists");
}

bool BlockchainSQLiteDB::tx_exists(const crypto::hash& h, uint64_t& tx_id) const {
    M2_STUB("tx_exists");
}

uint64_t BlockchainSQLiteDB::get_tx_unlock_time(const crypto::hash& h) const {
    M2_STUB("get_tx_unlock_time");
}

bool BlockchainSQLiteDB::get_tx_blob(const crypto::hash& h, std::string& tx) const {
    M2_STUB("get_tx_blob");
}

bool BlockchainSQLiteDB::get_pruned_tx_blob(const crypto::hash& h, std::string& tx) const {
    M2_STUB("get_pruned_tx_blob");
}

bool BlockchainSQLiteDB::get_pruned_tx_blobs_from( const crypto::hash& h, size_t count, std::vector<std::string>& bd) const {
    M2_STUB("get_pruned_tx_blobs_from");
}

bool BlockchainSQLiteDB::get_prunable_tx_blob(const crypto::hash& h, std::string& tx) const {
    M2_STUB("get_prunable_tx_blob");
}

bool BlockchainSQLiteDB::get_prunable_tx_hash( const crypto::hash& tx_hash, crypto::hash& prunable_hash) const {
    M2_STUB("get_prunable_tx_hash");
}

uint64_t BlockchainSQLiteDB::get_tx_count() const {
    M2_STUB("get_tx_count");
}

std::vector<transaction> BlockchainSQLiteDB::get_tx_list(const std::vector<crypto::hash>& hlist) const {
    M2_STUB("get_tx_list");
}

std::vector<uint64_t> BlockchainSQLiteDB::get_tx_block_heights( const std::vector<crypto::hash>& h) const {
    M2_STUB("get_tx_block_heights");
}

uint64_t BlockchainSQLiteDB::get_num_outputs(const uint64_t& amount) const {
    M2_STUB("get_num_outputs");
}

output_data_t BlockchainSQLiteDB::get_output_key( const uint64_t& amount, const uint64_t& index, bool include_commitmemt) const {
    M2_STUB("get_output_key");
}

tx_out_index BlockchainSQLiteDB::get_output_tx_and_index_from_global(const uint64_t& index) const {
    M2_STUB("get_output_tx_and_index_from_global");
}

tx_out_index BlockchainSQLiteDB::get_output_tx_and_index( const uint64_t& amount, const uint64_t& index) const {
    M2_STUB("get_output_tx_and_index");
}

void BlockchainSQLiteDB::get_output_tx_and_index( const uint64_t& amount, const std::vector<uint64_t>& offsets, std::vector<tx_out_index>& indices) const {
    M2_STUB("get_output_tx_and_index");
}

void BlockchainSQLiteDB::get_output_key( const epee::span<const uint64_t>& amounts, const std::vector<uint64_t>& offsets, std::vector<output_data_t>& outputs, bool allow_partial) const {
    M2_STUB("get_output_key");
}

bool BlockchainSQLiteDB::can_thread_bulk_indices() const {
    M2_STUB("can_thread_bulk_indices");
}

std::vector<std::vector<uint64_t>> BlockchainSQLiteDB::get_tx_amount_output_indices( const uint64_t tx_id, size_t n_txes) const {
    M2_STUB("get_tx_amount_output_indices");
}

bool BlockchainSQLiteDB::has_key_image(const crypto::key_image& img) const {
    M2_STUB("has_key_image");
}

void BlockchainSQLiteDB::add_txpool_tx( const crypto::hash& txid, const std::string& blob, const txpool_tx_meta_t& details) {
    M2_STUB("add_txpool_tx");
}

void BlockchainSQLiteDB::update_txpool_tx(const crypto::hash& txid, const txpool_tx_meta_t& details) {
    M2_STUB("update_txpool_tx");
}

uint64_t BlockchainSQLiteDB::get_txpool_tx_count(bool include_unrelayed_txes) const {
    M2_STUB("get_txpool_tx_count");
}

bool BlockchainSQLiteDB::txpool_has_tx(const crypto::hash& txid) const {
    M2_STUB("txpool_has_tx");
}

void BlockchainSQLiteDB::remove_txpool_tx(const crypto::hash& txid) {
    M2_STUB("remove_txpool_tx");
}

bool BlockchainSQLiteDB::get_txpool_tx_meta(const crypto::hash& txid, txpool_tx_meta_t& meta) const {
    M2_STUB("get_txpool_tx_meta");
}

bool BlockchainSQLiteDB::get_txpool_tx_blob(const crypto::hash& txid, std::string& bd) const {
    M2_STUB("get_txpool_tx_blob");
}

std::string BlockchainSQLiteDB::get_txpool_tx_blob(const crypto::hash& txid) const {
    M2_STUB("get_txpool_tx_blob");
}

void BlockchainSQLiteDB::prune_outputs(uint64_t amount) {
    M2_STUB("prune_outputs");
}

uint32_t BlockchainSQLiteDB::get_blockchain_pruning_seed() const {
    M2_STUB("get_blockchain_pruning_seed");
}

bool BlockchainSQLiteDB::prune_blockchain(uint32_t pruning_seed) {
    M2_STUB("prune_blockchain");
}

bool BlockchainSQLiteDB::update_pruning() {
    M2_STUB("update_pruning");
}

bool BlockchainSQLiteDB::check_pruning() {
    M2_STUB("check_pruning");
}

uint64_t BlockchainSQLiteDB::get_max_block_size() {
    M2_STUB("get_max_block_size");
}

void BlockchainSQLiteDB::add_max_block_size(uint64_t sz) {
    M2_STUB("add_max_block_size");
}

void BlockchainSQLiteDB::add_alt_block( const crypto::hash& blkid, const cryptonote::alt_block_data_t& data, const std::string& blob, const std::string* checkpoint) {
    M2_STUB("add_alt_block");
}

bool BlockchainSQLiteDB::get_alt_block( const crypto::hash& blkid, alt_block_data_t* data, std::string* blob, std::string* checkpoint) const {
    M2_STUB("get_alt_block");
}

void BlockchainSQLiteDB::remove_alt_block(const crypto::hash& blkid) {
    M2_STUB("remove_alt_block");
}

uint64_t BlockchainSQLiteDB::get_alt_block_count() {
    M2_STUB("get_alt_block_count");
}

void BlockchainSQLiteDB::drop_alt_blocks() {
    M2_STUB("drop_alt_blocks");
}

bool BlockchainSQLiteDB::for_all_txpool_txes( std::function<bool(const crypto::hash&, const txpool_tx_meta_t&, const std::string*)>, bool include_blob, bool include_unrelayed_txes) const {
    M2_STUB("for_all_txpool_txes");
}

bool BlockchainSQLiteDB::for_all_key_images(std::function<bool(const crypto::key_image&)>) const {
    M2_STUB("for_all_key_images");
}

bool BlockchainSQLiteDB::for_blocks_range( const uint64_t& h1, const uint64_t& h2, std::function<bool(uint64_t, const crypto::hash&, const cryptonote::block&)>) const {
    M2_STUB("for_blocks_range");
}

bool BlockchainSQLiteDB::for_all_transactions( std::function<bool(const crypto::hash&, const cryptonote::transaction&)>, bool pruned) const {
    M2_STUB("for_all_transactions");
}

bool BlockchainSQLiteDB::for_all_outputs( std::function<bool( uint64_t amount, const crypto::hash& tx_hash, uint64_t height, size_t tx_idx)> f) const {
    M2_STUB("for_all_outputs");
}

bool BlockchainSQLiteDB::for_all_outputs( uint64_t amount, const std::function<bool(uint64_t height)>& f) const {
    M2_STUB("for_all_outputs");
}

bool BlockchainSQLiteDB::for_all_alt_blocks( std::function< bool(const crypto::hash& blkid, const alt_block_data_t& data, const std::string* block_blob, const std::string* checkpoint_blob)> f, bool include_blob) const {
    M2_STUB("for_all_alt_blocks");
}

std::map<uint64_t, std::tuple<uint64_t, uint64_t, uint64_t>> BlockchainSQLiteDB::get_output_histogram( const std::vector<uint64_t>& amounts, bool unlocked, uint64_t recent_cutoff, uint64_t min_count) const {
    M2_STUB("get_output_histogram");
}

bool BlockchainSQLiteDB::get_output_distribution( uint64_t amount, uint64_t from_height, uint64_t to_height, std::vector<uint64_t>& distribution, uint64_t& base) const {
    M2_STUB("get_output_distribution");
}

bool BlockchainSQLiteDB::is_read_only() const {
    return m_read_only;
}

uint64_t BlockchainSQLiteDB::get_database_size() const {
    M2_STUB("get_database_size");
}

void BlockchainSQLiteDB::get_output_blacklist(std::vector<uint64_t>& blacklist) const {
    M2_STUB("get_output_blacklist");
}

void BlockchainSQLiteDB::add_output_blacklist(std::vector<uint64_t> const& blacklist) {
    M2_STUB("add_output_blacklist");
}

void BlockchainSQLiteDB::set_service_node_data(const std::string& data, bool long_term) {
    M2_STUB("set_service_node_data");
}

bool BlockchainSQLiteDB::get_service_node_data(std::string& data, bool long_term) const {
    M2_STUB("get_service_node_data");
}

void BlockchainSQLiteDB::clear_service_node_data() {
    M2_STUB("clear_service_node_data");
}

bool BlockchainSQLiteDB::get_service_node_proof( const crypto::public_key& pubkey, service_nodes::proof_info& proof) const {
    M2_STUB("get_service_node_proof");
}

std::unordered_map<crypto::public_key, service_nodes::proof_info> BlockchainSQLiteDB::get_all_service_node_proofs() const {
    M2_STUB("get_all_service_node_proofs");
}

void BlockchainSQLiteDB::set_service_node_proof( const crypto::public_key& pubkey, const service_nodes::proof_info& proof) {
    M2_STUB("set_service_node_proof");
}

bool BlockchainSQLiteDB::remove_service_node_proof(const crypto::public_key& pubkey) {
    M2_STUB("remove_service_node_proof");
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#undef M2_STUB

}  // namespace cryptonote
