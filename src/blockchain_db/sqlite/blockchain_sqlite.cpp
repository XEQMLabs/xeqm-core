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
// Combined SQLite BlockchainDB backend -- M2a (data-access foundation + core chain path).
// See blockchain_sqlite.h and docs/sqlite-port-M2-notes.md for scope.  Methods tied to
// xeqm's diverged rewards / service-node / consensus state throw TODO(M2b).
// =====================================================================================

#include "blockchain_sqlite.h"

#include <fmt/std.h>
#include <oxenc/endian.h>
#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "checkpoints/checkpoints.h"
#include "common/format.h"
#include "common/guts.h"
#include "cryptonote_basic/cryptonote_format_utils.h"
#include "cryptonote_config.h"
#include "cryptonote_core/service_node_list.h"
#include "cryptonote_core/uptime_proof.h"
#include "logging/oxen_logger.h"
#include "ringct/rctOps.h"
#include "serialization/binary_utils.h"
#include "serialization/serialization.h"

namespace cryptonote {

using namespace std::literals;
using db::blob_guts;

namespace {

    auto logcat = oxen::log::Cat("blockchain.db.sqlite");

    // PRAGMA user_version of the current schema.  v1 = M2a core chain tables; v2 = M2b-1 adds the
    // quorum-signed checkpoint tables and the service-node blob/proof stores (see SCHEMA_V2).
    constexpr int SCHEMA_VERSION = 2;

    // Upper bound on the values bound into one `IN (...)` list.
    constexpr size_t MAX_IN_LIST = 500;

    // --- small bit-cast helpers between SQLite's signed INTEGER and our unsigned values ---
    constexpr int64_t as_i64(uint64_t x) {
        return static_cast<int64_t>(x);
    }
    constexpr uint64_t as_u64(int64_t x) {
        return static_cast<uint64_t>(x);
    }

    // Binds a trivially-copyable value's bytes as a BLOB parameter.
    template <typename T>
    db::blob_binder as_guts(const T& v) {
        return db::bind_guts(v);
    }
    // Binds a byte string as a BLOB parameter (no-copy).
    db::blob_binder as_blob(std::string_view s) {
        return db::blob_binder{s};
    }

    // Copies a BLOB column into a std::string.
    std::string col_str(const SQLite::Column& c) {
        return std::string{static_cast<const char*>(c.getBlob()), static_cast<size_t>(c.getBytes())};
    }

    // Core chain tables only.  The diverged rewards / service-node tables (and the quorum-signed
    // checkpoint tables, and the light-node history/trimmed-tx tables) are NOT created here: see
    // docs/sqlite-port-M2-notes.md.  Foreign keys from the per-block tables to `blocks` are
    // deferred because BlockchainDB::add_block writes a block's transactions, outputs and key
    // images before the block's own row, all in one write transaction; the ON DELETE CASCADE still
    // fires immediately, so popping a block is a single delete of its `blocks` row.
    constexpr auto SCHEMA = R"(
CREATE TABLE blocks (
    height                INTEGER PRIMARY KEY,
    hash                  BLOB    NOT NULL CHECK(length(hash) = 32),
    hash8                 BLOB    GENERATED ALWAYS AS (substr(hash, 1, 8)) VIRTUAL,
    timestamp             INTEGER NOT NULL,
    weight                INTEGER NOT NULL,
    long_term_weight      INTEGER NOT NULL,
    cumulative_difficulty INTEGER NOT NULL,
    coins_generated       INTEGER NOT NULL,
    rct_outputs           INTEGER NOT NULL,
    blob                  BLOB
) STRICT;
CREATE INDEX blocks_hash8 ON blocks(hash8);

CREATE TABLE transactions (
    id            INTEGER PRIMARY KEY,
    hash          BLOB    NOT NULL CHECK(length(hash) = 32),
    hash8         BLOB    GENERATED ALWAYS AS (substr(hash, 1, 8)) VIRTUAL,
    height        INTEGER NOT NULL REFERENCES blocks(height) ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED,
    type          INTEGER NOT NULL,
    miner         INTEGER NOT NULL DEFAULT 0,
    unlock_time   INTEGER NOT NULL,
    unprunable    BLOB    NOT NULL,
    prunable      BLOB,
    prunable_hash BLOB    CHECK(prunable_hash IS NULL OR length(prunable_hash) = 32)
) STRICT;
CREATE INDEX transactions_height ON transactions(height);
CREATE INDEX transactions_hash8  ON transactions(hash8);

CREATE TABLE outputs (
    id           INTEGER PRIMARY KEY,
    amount       INTEGER NOT NULL DEFAULT 0,
    amount_index INTEGER NOT NULL,
    height       INTEGER NOT NULL REFERENCES blocks(height) ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED,
    unlock_time  INTEGER NOT NULL,
    pubkey       BLOB    NOT NULL CHECK(length(pubkey) = 32),
    commitment   BLOB    NOT NULL CHECK(length(commitment) = 32),
    tx_id        INTEGER,
    local_index  INTEGER,
    blacklisted  INTEGER NOT NULL DEFAULT 0
) STRICT;
CREATE UNIQUE INDEX outputs_amount_index ON outputs(amount, amount_index);
CREATE INDEX        outputs_height       ON outputs(height);
CREATE INDEX        outputs_blacklisted  ON outputs(id) WHERE blacklisted;
CREATE INDEX        outputs_tx           ON outputs(tx_id, local_index) WHERE tx_id IS NOT NULL;

CREATE TABLE key_images (
    key_image BLOB    PRIMARY KEY CHECK(length(key_image) = 32),
    height    INTEGER NOT NULL REFERENCES blocks(height) ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED
) STRICT, WITHOUT ROWID;
CREATE INDEX key_images_height ON key_images(height);

CREATE TABLE properties (
    key   TEXT PRIMARY KEY,
    value ANY NOT NULL
) STRICT, WITHOUT ROWID;

CREATE TABLE txpool (
    hash                  BLOB    PRIMARY KEY CHECK(length(hash) = 32),
    blob                  BLOB    NOT NULL,
    weight                INTEGER NOT NULL,
    fee                   INTEGER NOT NULL,
    receive_time          INTEGER NOT NULL,
    last_relayed_time     INTEGER NOT NULL,
    max_used_block_height INTEGER NOT NULL,
    max_used_block_hash   BLOB    NOT NULL,
    last_failed_height    INTEGER NOT NULL,
    last_failed_hash      BLOB    NOT NULL,
    l2_height             INTEGER NOT NULL,
    kept_by_block         INTEGER NOT NULL,
    relayed               INTEGER NOT NULL,
    do_not_relay          INTEGER NOT NULL,
    double_spend_seen     INTEGER NOT NULL
) STRICT, WITHOUT ROWID;

CREATE TABLE alt_blocks (
    hash                  BLOB    PRIMARY KEY CHECK(length(hash) = 32),
    height                INTEGER NOT NULL,
    prev_hash             BLOB    NOT NULL CHECK(length(prev_hash) = 32),
    cumulative_weight     INTEGER NOT NULL,
    cumulative_difficulty INTEGER NOT NULL,
    coins_generated       INTEGER NOT NULL,
    blob                  BLOB    NOT NULL,
    checkpoint            BLOB
) STRICT, WITHOUT ROWID;
CREATE INDEX alt_blocks_height ON alt_blocks(height);
)";

    // Schema v2 (M2b-1): quorum-signed checkpoints and the service-node data/proof stores.  Applied
    // in full on a fresh database, and as a migration on a v1 (M2a) database (see open()).
    //
    // Checkpoints mirror BlockchainLMDB's block_checkpoints: `type` is stored (hardcoded vs
    // service_node); service-node checkpoints additionally carry quorum signatures, deleted via the
    // ON DELETE CASCADE when a checkpoint is replaced or removed.  There is deliberately no foreign
    // key to `blocks`: hardcoded checkpoints are written at startup, long before the chain reaches
    // their heights.
    //
    // Service-node data/proofs are stored exactly as BlockchainLMDB stores them: `service_node_data`
    // holds the two opaque (short-/long-term) SNL state blobs by a fixed key, and `service_node_proofs`
    // holds each proof as the fixed-size POD-serialised blob keyed by the SN pubkey.  Their internal
    // schema is not parsed here (it is owned by the service-node layer), only stored.
    constexpr auto SCHEMA_V2 = R"(
CREATE TABLE checkpoints (
    height INTEGER PRIMARY KEY,
    hash   BLOB    NOT NULL CHECK(length(hash) = 32),
    type   INTEGER NOT NULL
) STRICT;

CREATE TABLE checkpoint_signatures (
    height      INTEGER NOT NULL REFERENCES checkpoints(height) ON DELETE CASCADE,
    voter_index INTEGER NOT NULL,
    signature   BLOB    NOT NULL CHECK(length(signature) = 64),
    PRIMARY KEY(height, voter_index)
) STRICT, WITHOUT ROWID;

CREATE TABLE service_node_data (
    id   INTEGER PRIMARY KEY,   -- SERVICE_NODE_BLOB_SHORT_TERM_KEY / SERVICE_NODE_BLOB_LONG_TERM_KEY
    data BLOB    NOT NULL
) STRICT, WITHOUT ROWID;

CREATE TABLE service_node_proofs (
    pubkey BLOB PRIMARY KEY CHECK(length(pubkey) = 32),
    proof  BLOB NOT NULL
) STRICT, WITHOUT ROWID;
)";

    // The fixed keys BlockchainLMDB uses for the two service-node state blobs (see db_lmdb.cpp).
    constexpr int64_t SERVICE_NODE_BLOB_SHORT_TERM_KEY = 1;
    constexpr int64_t SERVICE_NODE_BLOB_LONG_TERM_KEY = 2;

    // Core tables in the order they can be emptied without violating a foreign key.
    constexpr std::array ALL_TABLES{
            "key_images",
            "outputs",
            "transactions",
            "blocks",
            "txpool",
            "alt_blocks",
            "checkpoint_signatures",
            "checkpoints",
            "service_node_data",
            "service_node_proofs"};

    // The database file and the write-ahead log / shared memory files SQLite keeps beside it.
    std::array<fs::path, 3> db_files(const fs::path& db) {
        std::array<fs::path, 3> files{db, db, db};
        files[1] += "-wal";
        files[2] += "-shm";
        return files;
    }

    uint64_t chain_height(db::Database& db) {
        return as_u64(db.prepared_get<int64_t>("SELECT COALESCE(MAX(height) + 1, 0) FROM blocks"));
    }

    uint64_t num_outputs(db::Database& db, int64_t amount) {
        return as_u64(db.prepared_get<int64_t>(
                "SELECT COALESCE(MAX(amount_index) + 1, 0) FROM outputs WHERE amount = ?", amount));
    }

    template <typename T>
        requires std::is_same_v<T, block> || std::is_same_v<T, block_header> ||
                 std::is_same_v<T, std::string>
    T convert_block_blob(std::string_view blob) {
        T result;
        if constexpr (std::is_same_v<T, block>) {
            if (!parse_and_validate_block_from_blob(blob, result))
                throw DB_ERROR("Failed to parse block from blob retrieved from the db");
        } else if constexpr (std::is_same_v<T, block_header>) {
            serialization::binary_string_unarchiver ba{blob};
            serialization::value(ba, result);
        } else {
            result = blob;
        }
        return result;
    }

    // Reads a tx's full blob into `bd`; false if the tx, or its prunable part, is not stored.
    bool load_tx_blob(db::Database& db, const crypto::hash& h, std::string& bd) {
        auto st = db.prepared_bind(
                "SELECT unprunable, prunable FROM transactions "
                "WHERE hash8 = substr(?1, 1, 8) AND hash = ?1",
                as_guts(h));
        if (!st->executeStep())
            return false;
        auto unprunable = st->getColumn(0);
        auto prunable = st->getColumn(1);
        if (prunable.isNull())
            return false;
        bd.reserve(bd.size() + unprunable.getBytes() + prunable.getBytes());
        bd.append(static_cast<const char*>(unprunable.getBlob()), unprunable.getBytes());
        bd.append(static_cast<const char*>(prunable.getBlob()), prunable.getBytes());
        return true;
    }

    // --- txpool metadata column handling ----------------------------------------------------
    constexpr auto TXPOOL_META_COLUMNS =
            "weight, fee, receive_time, last_relayed_time, max_used_block_height, "
            "max_used_block_hash, last_failed_height, last_failed_hash, l2_height, kept_by_block, "
            "relayed, do_not_relay, double_spend_seen";
    // The 13 '?' placeholders for the columns above.
    constexpr auto TXPOOL_META_PLACEHOLDERS = "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?";

    txpool_tx_meta_t read_txpool_meta(SQLite::Statement& st, int col) {
        auto u64 = [&](int i) { return as_u64(st.getColumn(col + i).getInt64()); };
        auto u8 = [&](int i) { return static_cast<uint8_t>(st.getColumn(col + i).getInt()); };
        auto hash = [&](int i) -> crypto::hash {
            return blob_guts<crypto::hash>{st.getColumn(col + i)};
        };
        txpool_tx_meta_t meta{};
        meta.weight = u64(0);
        meta.fee = u64(1);
        meta.receive_time = u64(2);
        meta.last_relayed_time = u64(3);
        meta.max_used_block_height = u64(4);
        meta.max_used_block_id = hash(5);
        meta.last_failed_height = u64(6);
        meta.last_failed_id = hash(7);
        meta.l2_height = u64(8);
        meta.kept_by_block = u8(9);
        meta.relayed = u8(10);
        meta.do_not_relay = u8(11);
        meta.double_spend_seen = u8(12);
        return meta;
    }

    // --- alt-block data column handling -----------------------------------------------------
    constexpr auto ALT_BLOCK_DATA_COLUMNS =
            "height, cumulative_weight, cumulative_difficulty, coins_generated, "
            "checkpoint IS NOT NULL";

    alt_block_data_t read_alt_block_data(SQLite::Statement& st, int col) {
        alt_block_data_t data{};
        data.height = as_u64(st.getColumn(col).getInt64());
        data.cumulative_weight = as_u64(st.getColumn(col + 1).getInt64());
        data.cumulative_difficulty = as_u64(st.getColumn(col + 2).getInt64());
        data.already_generated_coins = as_u64(st.getColumn(col + 3).getInt64());
        data.checkpointed = static_cast<uint8_t>(st.getColumn(col + 4).getInt());
        return data;
    }

    // --- checkpoint column handling ---------------------------------------------------------
    // Reads a checkpoint whose `checkpoints` row has already been located, loading its signatures
    // (if any) from checkpoint_signatures.  Mirrors BlockchainLMDB's convert_mdb_val_to_checkpoint.
    checkpoint_t load_checkpoint(
            db::Database& db, int64_t height, const crypto::hash& hash, checkpoint_type type) {
        checkpoint_t checkpoint{};
        checkpoint.type = type;
        checkpoint.height = as_u64(height);
        checkpoint.block_hash = hash;
        for (auto row : db.prepared_results<int64_t, blob_guts<crypto::signature>>(
                     "SELECT voter_index, signature FROM checkpoint_signatures WHERE height = ? "
                     "ORDER BY voter_index",
                     height)) {
            auto& [voter_index, signature] = row;
            checkpoint.signatures.emplace_back(
                    static_cast<uint16_t>(voter_index), signature.value);
        }
        return checkpoint;
    }

    // --- service-node proof serialisation ---------------------------------------------------
    // Byte-for-byte the same fixed-size POD layout BlockchainLMDB stores (so the blob we keep in the
    // service_node_proofs table is identical to the one LMDB keeps).  The service-node layer owns the
    // meaning of these fields; this backend only serialises/deserialises them.
    template <typename C>
    C host_to_little_container(const C& c) {
        C result{c};
        for (auto& x : result)
            oxenc::host_to_little_inplace(x);
        return result;
    }
    template <typename C>
    C little_to_host_container(const C& c) {
        C result{c};
        for (auto& x : result)
            oxenc::little_to_host_inplace(x);
        return result;
    }

    struct service_node_proof_serialized_old {
        service_node_proof_serialized_old() = default;
        service_node_proof_serialized_old(const service_nodes::proof_info& info) :
                timestamp{oxenc::host_to_little(info.timestamp)},
                ip{oxenc::host_to_little(info.proof->public_ip)},
                storage_https_port{oxenc::host_to_little(info.proof->storage_https_port)},
                quorumnet_port{oxenc::host_to_little(info.proof->qnet_port)},
                version{host_to_little_container(info.proof->version)},
                storage_omq_port{oxenc::host_to_little(info.proof->storage_omq_port)},
                pubkey_ed25519{info.proof->pubkey_ed25519} {}

        void update(service_nodes::proof_info& info) const {
            info.timestamp = oxenc::little_to_host(timestamp);
            if (info.timestamp > info.effective_timestamp)
                info.effective_timestamp = info.timestamp;
            info.proof->public_ip = oxenc::little_to_host(ip);
            info.proof->storage_https_port = oxenc::little_to_host(storage_https_port);
            info.proof->storage_omq_port = oxenc::little_to_host(storage_omq_port);
            info.proof->qnet_port = oxenc::little_to_host(quorumnet_port);
            info.proof->version = little_to_host_container(version);
            info.proof->storage_server_version = {0, 0, 0};
            info.proof->lokinet_version = {0, 0, 0};
            info.update_pubkey(pubkey_ed25519);
        }

        uint64_t timestamp;
        uint32_t ip;
        uint16_t storage_https_port;
        uint16_t quorumnet_port;
        std::array<uint16_t, 3> version;
        uint16_t storage_omq_port;
        crypto::ed25519_public_key pubkey_ed25519;
    };
    static_assert(
            sizeof(service_node_proof_serialized_old) == 56,
            "service node serialization struct has unexpected size and/or padding");

    struct service_node_proof_serialized : service_node_proof_serialized_old {
        service_node_proof_serialized() = default;
        service_node_proof_serialized(const service_nodes::proof_info& info) :
                service_node_proof_serialized_old{info},
                storage_server_version{
                        host_to_little_container(info.proof->storage_server_version)},
                lokinet_version{host_to_little_container(info.proof->lokinet_version)} {}
        std::array<uint16_t, 3> storage_server_version{};
        std::array<uint16_t, 3> lokinet_version{};
        char _padding[4]{};

        void update(service_nodes::proof_info& info) const {
            if (!info.proof)
                info.proof = std::make_unique<uptime_proof::Proof>();
            service_node_proof_serialized_old::update(info);
            info.proof->storage_server_version =
                    little_to_host_container(storage_server_version);
            info.proof->lokinet_version = little_to_host_container(lokinet_version);
            // BLS pubkey & pop are only temporary during the HF20 transition period, so we don't
            // store or retrieve them, which means they don't persist across a restart.
        }
    };
    static_assert(
            sizeof(service_node_proof_serialized) == 72,
            "service node serialization struct has unexpected size and/or padding");

    // Decodes a stored proof blob (either the 56-byte legacy or 72-byte current layout) into `proof`.
    void load_service_node_proof(std::string_view blob, service_nodes::proof_info& proof) {
        if (blob.size() == sizeof(service_node_proof_serialized_old)) {
            service_node_proof_serialized_old s;
            std::memcpy(&s, blob.data(), sizeof(s));
            s.update(proof);
        } else if (blob.size() >= sizeof(service_node_proof_serialized)) {
            service_node_proof_serialized s;
            std::memcpy(&s, blob.data(), sizeof(s));
            s.update(proof);
        } else {
            throw DB_ERROR("Service node proof blob has unexpected size {}"_format(blob.size()));
        }
    }

}  // namespace

void BlockchainSQLiteDB::m2_stub(const char* method) {
    throw std::runtime_error(std::string{"BlockchainSQLite: not implemented (M2b): "} + method);
}

BlockchainSQLiteDB::BlockchainSQLiteDB(bool batch_transactions) :
        m_batch_transactions{batch_transactions} {}

BlockchainSQLiteDB::~BlockchainSQLiteDB() {
    if (m_open) {
        try {
            close();
        } catch (const std::exception& e) {
            oxen::log::error(logcat, "Failed to close the blockchain database: {}", e.what());
        }
    }
}

#define M2_STUB(method) m2_stub(method)

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

// =========================================================================================
// Lifecycle
// =========================================================================================

void BlockchainSQLiteDB::check_open() const {
    if (!m_open)
        throw DB_ERROR("DB operation attempted on a not-open DB instance");
}

void BlockchainSQLiteDB::open(
        const fs::path& filename, cryptonote::network_type nettype, const int db_flags) {
    if (m_open)
        throw DB_OPEN_FAILURE("Attempted to open db, but it's already open");

    m_read_only = db_flags & DBF_RDONLY;
    // NB: db::Database always opens OPEN_READWRITE|OPEN_CREATE; a true read-only open (and the
    // --db-sync-mode handling) is left for a later milestone.  WAL + synchronous=NORMAL +
    // foreign_keys=ON are set by the db::Database constructor, which is what this schema needs.
    try {
        if (filename.has_parent_path())
            fs::create_directories(filename.parent_path());

        m_db = std::make_unique<db::Database>(filename, ""sv);

        int version = m_db->db.execAndGet("PRAGMA user_version").getInt();
        if (version > SCHEMA_VERSION)
            throw DB_OPEN_FAILURE(
                    "{} has schema version {}, but this build only supports up to {}; it was "
                    "probably created by a newer version"_format(filename, version, SCHEMA_VERSION));
        if (version == 0) {
            SQLite::Transaction tx{m_db->db, SQLite::TransactionBehavior::IMMEDIATE};
            m_db->db.exec(SCHEMA);
            m_db->db.exec(SCHEMA_V2);
            m_db->db.exec("PRAGMA user_version = {}"_format(SCHEMA_VERSION));
            tx.commit();
            oxen::log::info(logcat, "Created SQLite blockchain database {}", filename);
        } else if (version < SCHEMA_VERSION) {
            SQLite::Transaction tx{m_db->db, SQLite::TransactionBehavior::IMMEDIATE};
            if (version < 2)  // M2b-1: add checkpoint + service-node tables
                m_db->db.exec(SCHEMA_V2);
            m_db->db.exec("PRAGMA user_version = {}"_format(SCHEMA_VERSION));
            tx.commit();
            oxen::log::info(
                    logcat,
                    "Migrated SQLite blockchain database {} from schema v{} to v{}",
                    filename,
                    version,
                    SCHEMA_VERSION);
        }
    } catch (const DB_OPEN_FAILURE&) {
        m_db.reset();
        throw;
    } catch (const std::exception& e) {
        m_db.reset();
        throw DB_OPEN_FAILURE(
                "Failed to open blockchain database {}: {}"_format(filename, e.what()));
    }

    m_filename = filename;
    m_nettype = nettype;
    m_db_flags = db_flags;
    m_open = true;
}

void BlockchainSQLiteDB::close() {
    if (!m_open)
        return;
    if (m_write) {
        oxen::log::warning(
                logcat, "Closing the database with a write transaction open; rolling it back");
        end_write(false);
    }
    try {
        sync();
    } catch (const std::exception& e) {
        oxen::log::error(logcat, "Failed to sync the blockchain database on close: {}", e.what());
    }
    m_db.reset();
    m_open = false;
}

void BlockchainSQLiteDB::sync() {
    check_open();
    if (m_read_only)
        return;
    m_db->db.exec("PRAGMA wal_checkpoint(PASSIVE)");
}

// Deliberately nothing: synchronous=NORMAL (set at open) is already safe against corruption, and a
// FULL sync of every commit would only protect the last block, which a restart refetches.
void BlockchainSQLiteDB::safesyncmode(const bool) {}

void BlockchainSQLiteDB::reset() {
    check_open();
    if (m_writer.load() != std::thread::id{})
        throw DB_ERROR("Cannot reset the database while a write transaction is open");
    SQLite::Transaction tx{m_db->db, SQLite::TransactionBehavior::IMMEDIATE};
    for (auto table : ALL_TABLES)
        m_db->db.exec("DELETE FROM {}"_format(table));
    m_db->db.exec("DELETE FROM properties WHERE key = 'max_block_size'");
    tx.commit();
}

std::vector<fs::path> BlockchainSQLiteDB::get_filenames() const {
    auto files = db_files(m_filename);
    return {files.begin(), files.end()};
}

bool BlockchainSQLiteDB::remove_data_file(const fs::path& folder) const {
    for (auto& f : db_files(folder)) {
        std::error_code ec;
        fs::remove(f, ec);
        if (ec) {
            oxen::log::error(logcat, "Failed to remove {}: {}", f, ec.message());
            return false;
        }
    }
    return true;
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

bool BlockchainSQLiteDB::is_read_only() const {
    return m_read_only;
}

uint64_t BlockchainSQLiteDB::get_database_size() const {
    check_open();
    const auto files = db_files(m_filename);
    std::error_code ec;
    uint64_t size = fs::file_size(files[0], ec);
    if (ec)
        return 0;
    if (auto wal = fs::file_size(files[1], ec); !ec)
        size += wal;
    return size;
}

// =========================================================================================
// Transactions
// =========================================================================================

db::Database& BlockchainSQLiteDB::write_conn() {
    check_open();
    if (m_writer.load() != std::this_thread::get_id())
        throw DB_ERROR("Attempted a blockchain database write without an open write transaction");
    return *m_db;
}

void BlockchainSQLiteDB::begin_write(bool batch) {
    m_write.emplace(m_db->db, SQLite::TransactionBehavior::IMMEDIATE);
    m_batch_active = batch;
    m_writer = std::this_thread::get_id();
}

void BlockchainSQLiteDB::end_write(bool commit) {
    try {
        if (commit && m_write)
            m_write->commit();
    } catch (...) {
        m_write.reset();
        m_batch_active = false;
        m_writer = std::thread::id{};
        throw;
    }
    m_write.reset();  // rolls back if not committed
    m_batch_active = false;
    m_writer = std::thread::id{};
}

void BlockchainSQLiteDB::require_writer(std::string_view action) const {
    auto writer = m_writer.load();
    if (writer == std::thread::id{})
        throw DB_ERROR_TXN_START(
                "Attempted to {} write txn when no such txn exists"_format(action));
    if (writer != std::this_thread::get_id())
        throw DB_ERROR_TXN_START("Attempted to {} write txn from the wrong thread"_format(action));
}

void BlockchainSQLiteDB::require_batch_writer() const {
    if (!m_batch_transactions)
        throw DB_ERROR("batch transactions not enabled");
    if (!m_batch_active)
        throw DB_ERROR("batch transaction not in progress");
    if (m_writer.load() != std::this_thread::get_id())
        throw DB_ERROR("batch transaction owned by other thread");
}

bool BlockchainSQLiteDB::batch_start(uint64_t bytes_required) {
    if (!m_batch_transactions)
        throw DB_ERROR("batch transactions not enabled");
    if (m_batch_active)
        return false;
    if (m_writer.load() != std::thread::id{})
        throw DB_ERROR("batch transaction attempted, but a write transaction is already open");
    check_open();
    begin_write(true);
    return true;
}

void BlockchainSQLiteDB::batch_stop() {
    require_batch_writer();
    end_write(true);
}

void BlockchainSQLiteDB::batch_abort() {
    require_batch_writer();
    end_write(false);
}

void BlockchainSQLiteDB::set_batch_transactions(bool batch_transactions) {
    m_batch_transactions = batch_transactions;
}

void BlockchainSQLiteDB::block_wtxn_start() {
    check_open();
    auto writer = m_writer.load();
    if (writer == std::thread::id{}) {
        begin_write(false);
        return;
    }
    if (!m_batch_active)
        throw DB_ERROR_TXN_START("Attempted to start a write txn when one already exists");
    if (writer != std::this_thread::get_id())
        throw DB_ERROR_TXN_START(
                "Attempted to start a write txn when a batch txn exists in another thread");
}

void BlockchainSQLiteDB::block_wtxn_stop() {
    require_writer("stop");
    if (!m_batch_active)
        end_write(true);
}

void BlockchainSQLiteDB::block_wtxn_abort() {
    require_writer("abort");
    if (!m_batch_active)
        end_write(false);
}

// With a single shared connection there is no separate read-transaction snapshot to open: reads run
// in autocommit (serialised by OPEN_FULLMUTEX), and the writer thread's reads run inside its own
// write transaction (so they see its uncommitted writes).  See docs/sqlite-port-M2-notes.md for the
// concurrency caveat and the connection-pool work this leaves for a later milestone.
bool BlockchainSQLiteDB::block_rtxn_start() const {
    check_open();
    return false;
}
void BlockchainSQLiteDB::block_rtxn_stop() const {}
void BlockchainSQLiteDB::block_rtxn_abort() const {}

// =========================================================================================
// Blocks (write)
// =========================================================================================

void BlockchainSQLiteDB::add_block(
        const block& blk,
        size_t block_weight,
        uint64_t long_term_block_weight,
        const difficulty_type& cumulative_difficulty,
        const uint64_t& coins_generated,
        uint64_t num_rct_outs,
        const crypto::hash& blk_hash) {
    auto& db = write_conn();
    uint64_t height = chain_height(db);

    if (db.prepared_maybe_get<int64_t>(
                "SELECT height FROM blocks WHERE hash8 = substr(?1, 1, 8) AND hash = ?1",
                as_guts(blk_hash)))
        throw BLOCK_EXISTS("Attempting to add block that's already in the db");

    int64_t prev_rct_outputs = 0;
    if (height > 0) {
        auto row = db.prepared_get<blob_guts<crypto::hash>, int64_t>(
                "SELECT hash, rct_outputs FROM blocks WHERE height = ?", as_i64(height - 1));
        if (std::get<0>(row).value != blk.prev_id)
            throw BLOCK_PARENT_DNE("Top block is not new block's parent");
        prev_rct_outputs = std::get<1>(row);
    }

    std::string block_blob = block_to_blob(blk);
    db.prepared_exec(
            "INSERT INTO blocks (height, hash, timestamp, weight, long_term_weight, "
            "cumulative_difficulty, coins_generated, rct_outputs, blob) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
            as_i64(height),
            as_guts(blk_hash),
            as_i64(blk.timestamp),
            as_i64(block_weight),
            as_i64(long_term_block_weight),
            as_i64(cumulative_difficulty),
            as_i64(coins_generated),
            prev_rct_outputs + as_i64(num_rct_outs),
            as_blob(block_blob));
}

void BlockchainSQLiteDB::remove_block() {
    auto& db = write_conn();
    uint64_t height = chain_height(db);
    if (height == 0)
        throw BLOCK_DNE("Attempting to remove block from an empty blockchain");
    // Cascades to the block's transactions, outputs and key images.
    db.prepared_exec("DELETE FROM blocks WHERE height = ?", as_i64(height - 1));
}

void BlockchainSQLiteDB::pop_block(block& blk, std::vector<transaction>& txs) {
    check_open();
    // Unlike BlockchainDB::pop_block this reads everything it returns before removing anything,
    // because removing the block's row also removes (via the cascade) its transactions.
    block_wtxn_start();
    try {
        if (height() == 0)
            throw BLOCK_DNE("Attempting to remove block from an empty blockchain");
        blk = get_top_block();
        for (auto it = blk.tx_hashes.rbegin(); it != blk.tx_hashes.rend(); ++it) {
            transaction tx;
            if (!get_tx(*it, tx) && !get_pruned_tx(*it, tx))
                throw DB_ERROR("Failed to get pruned or unpruned transaction from the db");
            txs.push_back(std::move(tx));
        }
        remove_block();
        block_wtxn_stop();
    } catch (...) {
        block_wtxn_abort();
        throw;
    }
}

// =========================================================================================
// Blocks (read)
// =========================================================================================

bool BlockchainSQLiteDB::block_exists(const crypto::hash& h, uint64_t* height) const {
    check_open();
    auto found = m_db->prepared_maybe_get<int64_t>(
            "SELECT height FROM blocks WHERE hash8 = substr(?1, 1, 8) AND hash = ?1", as_guts(h));
    if (!found)
        return false;
    if (height)
        *height = as_u64(*found);
    return true;
}

uint64_t BlockchainSQLiteDB::get_block_height(const crypto::hash& h) const {
    uint64_t height;
    if (!block_exists(h, &height))
        throw BLOCK_DNE("Attempted to retrieve non-existent block height from hash {}"_format(h));
    return height;
}

template <typename T>
T BlockchainSQLiteDB::get_block_column(
        uint64_t height, std::string_view column, std::string_view what) const {
    check_open();
    auto value = m_db->prepared_maybe_get<T>(
            "SELECT {} FROM blocks WHERE height = ?"_format(column), as_i64(height));
    if (!value)
        throw BLOCK_DNE(
                "Attempt to get {} from height {} failed -- block not in db"_format(what, height));
    return *std::move(value);
}

template <typename T>
T BlockchainSQLiteDB::get_stored_block(uint64_t height, size_t* size) const {
    check_open();
    auto st = m_db->prepared_bind("SELECT blob FROM blocks WHERE height = ?", as_i64(height));
    if (!st->executeStep())
        throw BLOCK_DNE(
                "Attempt to get block from height {} failed -- block not in db"_format(height));
    auto blob = st->getColumn(0);
    if (blob.isNull())
        throw BLOCK_DNE("Block at height {} is not stored in the db"_format(height));
    std::string bd = col_str(blob);
    if (size)
        *size = bd.size();
    return convert_block_blob<T>(bd);
}

block BlockchainSQLiteDB::get_block_from_height(uint64_t height, size_t* size) const {
    return get_stored_block<block>(height, size);
}

block_header BlockchainSQLiteDB::get_block_header_from_height(uint64_t height) const {
    return get_stored_block<block_header>(height);
}

std::string BlockchainSQLiteDB::get_block_blob_from_height(uint64_t height) const {
    return get_stored_block<std::string>(height);
}

std::string BlockchainSQLiteDB::get_block_blob(const crypto::hash& h) const {
    return get_block_blob_from_height(get_block_height(h));
}

std::vector<uint64_t> BlockchainSQLiteDB::get_block_cumulative_rct_outputs(
        const std::vector<uint64_t>& heights) const {
    check_open();
    if (heights.empty())
        return {};
    const uint64_t top = chain_height(*m_db);
    for (auto h : heights)
        if (h >= top)
            throw BLOCK_DNE(
                    "Attempt to get rct distribution from height {} failed -- not in db"_format(h));
    std::vector<uint64_t> res;
    res.reserve(heights.size());
    for (auto h : heights)
        res.push_back(as_u64(m_db->prepared_get<int64_t>(
                "SELECT rct_outputs FROM blocks WHERE height = ?", as_i64(h))));
    return res;
}

uint64_t BlockchainSQLiteDB::get_block_timestamp(const uint64_t& height) const {
    return as_u64(get_block_column<int64_t>(height, "timestamp", "timestamp"));
}

uint64_t BlockchainSQLiteDB::get_top_block_timestamp() const {
    check_open();
    return as_u64(m_db->prepared_maybe_get<int64_t>(
                              "SELECT timestamp FROM blocks ORDER BY height DESC LIMIT 1")
                          .value_or(0));
}

size_t BlockchainSQLiteDB::get_block_weight(const uint64_t& height) const {
    return as_u64(get_block_column<int64_t>(height, "weight", "block weight"));
}

std::vector<uint64_t> BlockchainSQLiteDB::get_block_weight_column(
        std::string_view column, uint64_t start_height, size_t count) const {
    check_open();
    if (start_height >= chain_height(*m_db))
        throw DB_ERROR("Height {} not in blockchain"_format(start_height));
    std::vector<uint64_t> ret;
    for (auto w : m_db->prepared_results<int64_t>(
                 "SELECT {} FROM blocks WHERE height >= ? ORDER BY height LIMIT ?"_format(column),
                 as_i64(start_height),
                 as_i64(count)))
        ret.push_back(as_u64(w));
    return ret;
}

std::vector<uint64_t> BlockchainSQLiteDB::get_block_weights(
        uint64_t start_height, size_t count) const {
    return get_block_weight_column("weight", start_height, count);
}

std::vector<uint64_t> BlockchainSQLiteDB::get_long_term_block_weights(
        uint64_t start_height, size_t count) const {
    return get_block_weight_column("long_term_weight", start_height, count);
}

difficulty_type BlockchainSQLiteDB::get_block_cumulative_difficulty(const uint64_t& height) const {
    return as_u64(
            get_block_column<int64_t>(height, "cumulative_difficulty", "cumulative difficulty"));
}

difficulty_type BlockchainSQLiteDB::get_block_difficulty(const uint64_t& height) const {
    difficulty_type diff = get_block_cumulative_difficulty(height);
    if (height != 0)
        diff -= get_block_cumulative_difficulty(height - 1);
    return diff;
}

uint64_t BlockchainSQLiteDB::get_block_already_generated_coins(const uint64_t& height) const {
    return as_u64(get_block_column<int64_t>(height, "coins_generated", "generated coins"));
}

uint64_t BlockchainSQLiteDB::get_block_long_term_weight(const uint64_t& height) const {
    return as_u64(get_block_column<int64_t>(height, "long_term_weight", "long term weight"));
}

crypto::hash BlockchainSQLiteDB::get_block_hash_from_height(const uint64_t& height) const {
    return get_block_column<blob_guts<crypto::hash>>(height, "hash", "hash");
}

std::vector<block> BlockchainSQLiteDB::get_blocks_range(
        const uint64_t& h1, const uint64_t& h2) const {
    check_open();
    std::vector<block> v;
    if (h1 > h2)
        return v;
    for (uint64_t h = h1; h <= h2; ++h)
        v.push_back(get_block_from_height(h));
    return v;
}

std::vector<crypto::hash> BlockchainSQLiteDB::get_hashes_range(
        const uint64_t& h1, const uint64_t& h2) const {
    check_open();
    std::vector<crypto::hash> v;
    if (h1 > h2)
        return v;
    for (auto hash : m_db->prepared_results<blob_guts<crypto::hash>>(
                 "SELECT hash FROM blocks WHERE height BETWEEN ? AND ? ORDER BY height",
                 as_i64(h1),
                 as_i64(h2)))
        v.push_back(hash.value);
    if (v.size() != h2 - h1 + 1)
        throw BLOCK_DNE("Attempt to get hash from height {} failed -- hash not in db"_format(
                h1 + v.size()));
    return v;
}

crypto::hash BlockchainSQLiteDB::top_block_hash(uint64_t* block_height) const {
    check_open();
    auto top = m_db->prepared_maybe_get<int64_t, blob_guts<crypto::hash>>(
            "SELECT height, hash FROM blocks ORDER BY height DESC LIMIT 1");
    if (block_height)
        *block_height = top ? as_u64(std::get<0>(*top)) : uint64_t{0} - 1;
    if (!top)
        return crypto::null<crypto::hash>;
    return std::get<1>(*top).value;
}

block BlockchainSQLiteDB::get_top_block() const {
    check_open();
    auto top = m_db->prepared_maybe_get<int64_t>(
            "SELECT height FROM blocks ORDER BY height DESC LIMIT 1");
    if (!top)
        return block{};
    return get_block_from_height(as_u64(*top));
}

uint64_t BlockchainSQLiteDB::height() const {
    check_open();
    return chain_height(*m_db);
}

// =========================================================================================
// Transactions (write + read)
// =========================================================================================

uint64_t BlockchainSQLiteDB::add_transaction_data(
        const crypto::hash&,
        const std::pair<transaction, std::string>& txp,
        const crypto::hash& tx_hash,
        const crypto::hash& tx_prunable_hash) {
    auto& db = write_conn();

    if (auto id = db.prepared_maybe_get<int64_t>(
                "SELECT id FROM transactions WHERE hash8 = substr(?1, 1, 8) AND hash = ?1",
                as_guts(tx_hash)))
        throw TX_EXISTS(
                "Attempting to add transaction that's already in the db (tx id {})"_format(*id));

    const transaction& tx = txp.first;
    const std::string& blob = txp.second;

    size_t unprunable_size = tx.unprunable_size;
    if (unprunable_size == 0) {
        serialization::binary_string_archiver ba;
        try {
            const_cast<transaction&>(tx).serialize_base(ba);
        } catch (const std::exception& e) {
            throw DB_ERROR("Failed to serialize pruned tx: "s + e.what());
        }
        unprunable_size = ba.str().size();
    }
    if (unprunable_size > blob.size())
        throw DB_ERROR("pruned tx size is larger than tx size");

    std::string_view b{blob};
    std::optional<db::blob_binder> prunable_hash;
    if (tx.version >= txversion::v2_ringct)
        prunable_hash = as_guts(tx_prunable_hash);

    return as_u64(db.prepared_get<int64_t>(
            "INSERT INTO transactions (id, hash, height, type, miner, unlock_time, unprunable, "
            "prunable, prunable_hash) VALUES ((SELECT COALESCE(MAX(id) + 1, 0) FROM transactions), "
            "?, (SELECT COALESCE(MAX(height) + 1, 0) FROM blocks), ?, ?, ?, ?, ?, ?) RETURNING id",
            as_guts(tx_hash),
            static_cast<int>(tx.type),
            tx.is_miner_tx() ? 1 : 0,
            as_i64(tx.unlock_time),
            as_blob(b.substr(0, unprunable_size)),
            as_blob(b.substr(unprunable_size)),
            prunable_hash));
}

void BlockchainSQLiteDB::remove_transaction_data(const crypto::hash& tx_hash, const transaction&) {
    auto& db = write_conn();
    // Only reachable through BlockchainDB::remove_transaction, which this backend's pop_block does
    // not use (removing a block's row removes its transactions and outputs through the cascade), so
    // the rows may already be gone: that is not an error.
    db.prepared_exec(
            "DELETE FROM outputs WHERE tx_id = "
            "(SELECT id FROM transactions WHERE hash8 = substr(?1, 1, 8) AND hash = ?1)",
            as_guts(tx_hash));
    db.prepared_exec(
            "DELETE FROM transactions WHERE hash8 = substr(?1, 1, 8) AND hash = ?1",
            as_guts(tx_hash));
}

bool BlockchainSQLiteDB::tx_exists(const crypto::hash& h) const {
    uint64_t tx_id;
    return tx_exists(h, tx_id);
}

bool BlockchainSQLiteDB::tx_exists(const crypto::hash& h, uint64_t& tx_id) const {
    check_open();
    auto id = m_db->prepared_maybe_get<int64_t>(
            "SELECT id FROM transactions WHERE hash8 = substr(?1, 1, 8) AND hash = ?1", as_guts(h));
    if (!id)
        return false;
    tx_id = as_u64(*id);
    return true;
}

uint64_t BlockchainSQLiteDB::get_tx_unlock_time(const crypto::hash& h) const {
    check_open();
    auto unlock = m_db->prepared_maybe_get<int64_t>(
            "SELECT unlock_time FROM transactions WHERE hash8 = substr(?1, 1, 8) AND hash = ?1",
            as_guts(h));
    if (!unlock)
        throw TX_DNE("tx data with hash {} not found in db"_format(h));
    return as_u64(*unlock);
}

bool BlockchainSQLiteDB::get_tx_blob(const crypto::hash& h, std::string& tx) const {
    check_open();
    return load_tx_blob(*m_db, h, tx);
}

bool BlockchainSQLiteDB::get_pruned_tx_blob(const crypto::hash& h, std::string& bd) const {
    check_open();
    auto st = m_db->prepared_bind(
            "SELECT unprunable FROM transactions WHERE hash8 = substr(?1, 1, 8) AND hash = ?1",
            as_guts(h));
    if (!st->executeStep())
        return false;
    bd.assign(col_str(st->getColumn(0)));
    return true;
}

bool BlockchainSQLiteDB::get_pruned_tx_blobs_from(
        const crypto::hash& h, size_t count, std::vector<std::string>& bd) const {
    check_open();
    if (!count)
        return true;
    bd.reserve(bd.size() + count);
    size_t found = 0;
    auto st = m_db->prepared_bind(
            "SELECT unprunable FROM transactions "
            "WHERE id >= (SELECT id FROM transactions WHERE hash8 = substr(?1, 1, 8) AND hash = ?1) "
            "ORDER BY id LIMIT ?2",
            as_guts(h),
            as_i64(count));
    while (st->executeStep()) {
        bd.emplace_back(col_str(st->getColumn(0)));
        ++found;
    }
    return found == count;
}

bool BlockchainSQLiteDB::get_prunable_tx_blob(const crypto::hash& h, std::string& bd) const {
    check_open();
    auto st = m_db->prepared_bind(
            "SELECT prunable FROM transactions WHERE hash8 = substr(?1, 1, 8) AND hash = ?1",
            as_guts(h));
    if (!st->executeStep())
        return false;
    auto prunable = st->getColumn(0);
    if (prunable.isNull())
        return false;
    bd.assign(col_str(prunable));
    return true;
}

bool BlockchainSQLiteDB::get_prunable_tx_hash(
        const crypto::hash& tx_hash, crypto::hash& prunable_hash) const {
    check_open();
    auto st = m_db->prepared_bind(
            "SELECT prunable_hash FROM transactions WHERE hash8 = substr(?1, 1, 8) AND hash = ?1",
            as_guts(tx_hash));
    if (!st->executeStep())
        return false;
    auto col = st->getColumn(0);
    if (col.isNull())
        return false;
    prunable_hash = blob_guts<crypto::hash>{std::move(col)};
    return true;
}

uint64_t BlockchainSQLiteDB::get_tx_count() const {
    check_open();
    return as_u64(m_db->prepared_get<int64_t>("SELECT COALESCE(MAX(id) + 1, 0) FROM transactions"));
}

std::vector<transaction> BlockchainSQLiteDB::get_tx_list(
        const std::vector<crypto::hash>& hlist) const {
    check_open();
    std::vector<transaction> v;
    v.reserve(hlist.size());
    std::string bd;
    for (auto& h : hlist) {
        bd.clear();
        if (!load_tx_blob(*m_db, h, bd))
            throw TX_DNE("tx with hash {} not found in db"_format(h));
        if (!parse_and_validate_tx_from_blob(bd, v.emplace_back()))
            throw DB_ERROR("Failed to parse transaction from blob retrieved from the db");
    }
    return v;
}

std::vector<uint64_t> BlockchainSQLiteDB::get_tx_block_heights(
        const std::vector<crypto::hash>& hs) const {
    check_open();
    std::vector<uint64_t> result;
    result.reserve(hs.size());
    for (const auto& h : hs)
        result.push_back(as_u64(m_db->prepared_maybe_get<int64_t>(
                                        "SELECT height FROM transactions "
                                        "WHERE hash8 = substr(?1, 1, 8) AND hash = ?1",
                                        as_guts(h))
                                        .value_or(-1)));
    return result;
}

// =========================================================================================
// Outputs
// =========================================================================================

uint64_t BlockchainSQLiteDB::add_output(
        const crypto::hash& tx_hash,
        const tx_out& tx_output,
        const uint64_t& local_index,
        const uint64_t unlock_time,
        const rct::key* commitment) {
    auto& db = write_conn();

    if (!std::holds_alternative<txout_to_key>(tx_output.target))
        throw DB_ERROR("Wrong output type: expected txout_to_key");
    if (tx_output.amount == 0 && !commitment)
        throw DB_ERROR("RCT output without commitment");

    // Pre-RingCT outputs get the commitment they are implicitly verified against, so that every row
    // can be read the same way.
    const rct::key c = commitment ? *commitment : rct::zeroCommit(tx_output.amount);

    return as_u64(db.prepared_get<int64_t>(
            "INSERT INTO outputs (id, amount, amount_index, height, unlock_time, pubkey, "
            "commitment, tx_id, local_index) VALUES ("
            "(SELECT COALESCE(MAX(id) + 1, 0) FROM outputs), ?1, "
            "(SELECT COALESCE(MAX(amount_index) + 1, 0) FROM outputs WHERE amount = ?1), "
            "(SELECT COALESCE(MAX(height) + 1, 0) FROM blocks), ?2, ?3, ?4, "
            "(SELECT id FROM transactions WHERE hash8 = substr(?5, 1, 8) AND hash = ?5), ?6) "
            "RETURNING amount_index",
            as_i64(tx_output.amount),
            as_i64(unlock_time),
            as_guts(std::get<txout_to_key>(tx_output.target).key),
            as_guts(c),
            as_guts(tx_hash),
            as_i64(local_index)));
}

void BlockchainSQLiteDB::add_tx_amount_output_indices(
        const uint64_t, const std::vector<uint64_t>&) {
    // Nothing to store: each output row already records its transaction and position in it, which
    // is what get_tx_amount_output_indices reads.
}

std::vector<std::vector<uint64_t>> BlockchainSQLiteDB::get_tx_amount_output_indices(
        const uint64_t tx_id, size_t n_txes) const {
    check_open();
    std::vector<std::vector<uint64_t>> result(n_txes);
    if (!n_txes)
        return result;
    for (auto row : m_db->prepared_results<int64_t, int64_t>(
                 "SELECT tx_id, amount_index FROM outputs WHERE tx_id BETWEEN ? AND ? "
                 "ORDER BY tx_id, local_index",
                 as_i64(tx_id),
                 as_i64(tx_id + n_txes - 1)))
        result[as_u64(std::get<0>(row)) - tx_id].push_back(as_u64(std::get<1>(row)));
    return result;
}

uint64_t BlockchainSQLiteDB::get_num_outputs(const uint64_t& amount) const {
    check_open();
    return num_outputs(*m_db, as_i64(amount));
}

output_data_t BlockchainSQLiteDB::get_output_key(
        const uint64_t& amount, const uint64_t& index, bool include_commitment) const {
    check_open();
    output_data_t ret{};
    bool found = false;
    if (include_commitment) {
        if (auto row = m_db->prepared_maybe_get<
                    blob_guts<crypto::public_key>,
                    int64_t,
                    int64_t,
                    blob_guts<rct::key>>(
                    "SELECT pubkey, unlock_time, height, commitment FROM outputs "
                    "WHERE amount = ? AND amount_index = ?",
                    as_i64(amount),
                    as_i64(index))) {
            auto& [pubkey, unlock_time, height, commitment] = *row;
            ret = {pubkey.value, as_u64(unlock_time), as_u64(height), commitment.value};
            found = true;
        }
    } else if (
            auto row = m_db->prepared_maybe_get<blob_guts<crypto::public_key>, int64_t, int64_t>(
                    "SELECT pubkey, unlock_time, height FROM outputs "
                    "WHERE amount = ? AND amount_index = ?",
                    as_i64(amount),
                    as_i64(index))) {
        auto& [pubkey, unlock_time, height] = *row;
        ret.pubkey = pubkey.value;
        ret.unlock_time = as_u64(unlock_time);
        ret.height = as_u64(height);
        found = true;
    }
    if (!found)
        throw OUTPUT_DNE(
                "Attempting to get output pubkey by index, but key does not exist: amount {}, "
                "index {}"_format(amount, index));
    return ret;
}

void BlockchainSQLiteDB::get_output_key(
        const epee::span<const uint64_t>& amounts,
        const std::vector<uint64_t>& offsets,
        std::vector<output_data_t>& outputs,
        bool allow_partial) const {
    if (amounts.size() != 1 && amounts.size() != offsets.size())
        throw DB_ERROR("Invalid sizes of amounts and offsets");
    check_open();
    outputs.clear();
    outputs.reserve(offsets.size());

    auto amount_at = [&](size_t i) { return amounts.size() == 1 ? amounts[0] : amounts[i]; };

    std::unordered_map<uint64_t, std::vector<int64_t>> wanted;
    for (size_t i = 0; i < offsets.size(); ++i)
        wanted[amount_at(i)].push_back(as_i64(offsets[i]));

    std::unordered_map<uint64_t, std::unordered_map<uint64_t, output_data_t>> found;
    for (auto& [amount, indices] : wanted) {
        std::sort(indices.begin(), indices.end());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
        auto& found_amount = found[amount];
        for (size_t start = 0; start < indices.size(); start += MAX_IN_LIST) {
            auto end = std::min(indices.size(), start + MAX_IN_LIST);
            SQLite::Statement st{
                    m_db->db,
                    db::multi_in_query(
                            "SELECT amount_index, pubkey, unlock_time, height, commitment FROM "
                            "outputs WHERE amount = ? AND amount_index IN (",
                            end - start,
                            ")")};
            st.bind(1, as_i64(amount));
            int p = 2;
            for (size_t i = start; i < end; ++i)
                st.bind(p++, indices[i]);
            while (st.executeStep()) {
                auto amount_index = as_u64(st.getColumn(0).getInt64());
                crypto::public_key pubkey = blob_guts<crypto::public_key>{st.getColumn(1)};
                auto unlock_time = as_u64(st.getColumn(2).getInt64());
                auto out_height = as_u64(st.getColumn(3).getInt64());
                rct::key commitment = blob_guts<rct::key>{st.getColumn(4)};
                found_amount.emplace(
                        amount_index, output_data_t{pubkey, unlock_time, out_height, commitment});
            }
        }
    }

    for (size_t i = 0; i < offsets.size(); ++i) {
        const uint64_t amount = amount_at(i);
        auto& found_amount = found[amount];
        auto it = found_amount.find(offsets[i]);
        if (it == found_amount.end()) {
            if (allow_partial)
                break;
            throw OUTPUT_DNE(
                    "Attempting to get output pubkey by global index (amount {}, index {}), but "
                    "key does not exist (current height {})"_format(amount, offsets[i], height()));
        }
        outputs.push_back(it->second);
    }
}

tx_out_index BlockchainSQLiteDB::get_output_tx_and_index_from_global(const uint64_t& index) const {
    check_open();
    auto row = m_db->prepared_maybe_get<blob_guts<crypto::hash>, int64_t>(
            "SELECT t.hash, o.local_index FROM outputs o JOIN transactions t ON t.id = o.tx_id "
            "WHERE o.id = ?",
            as_i64(index));
    if (!row)
        throw OUTPUT_DNE("output with given index not in db");
    return {std::get<0>(*row).value, as_u64(std::get<1>(*row))};
}

tx_out_index BlockchainSQLiteDB::get_output_tx_and_index(
        const uint64_t& amount, const uint64_t& index) const {
    std::vector<tx_out_index> indices;
    get_output_tx_and_index(amount, {index}, indices);
    return indices.front();
}

void BlockchainSQLiteDB::get_output_tx_and_index(
        const uint64_t& amount,
        const std::vector<uint64_t>& offsets,
        std::vector<tx_out_index>& indices) const {
    check_open();
    indices.clear();
    indices.reserve(offsets.size());
    for (auto index : offsets) {
        auto row = m_db->prepared_maybe_get<blob_guts<crypto::hash>, int64_t>(
                "SELECT t.hash, o.local_index FROM outputs o "
                "JOIN transactions t ON t.id = o.tx_id WHERE o.amount = ? AND o.amount_index = ?",
                as_i64(amount),
                as_i64(index));
        if (!row)
            throw OUTPUT_DNE("Attempting to get output by index, but key does not exist");
        indices.emplace_back(std::get<0>(*row).value, as_u64(std::get<1>(*row)));
    }
}

bool BlockchainSQLiteDB::can_thread_bulk_indices() const {
    return true;
}

void BlockchainSQLiteDB::get_output_blacklist(std::vector<uint64_t>& blacklist) const {
    check_open();
    for (auto amount_index : m_db->prepared_results<int64_t>(
                 "SELECT amount_index FROM outputs WHERE blacklisted ORDER BY amount_index"))
        blacklist.push_back(as_u64(amount_index));
}

void BlockchainSQLiteDB::add_output_blacklist(std::vector<uint64_t> const& blacklist) {
    if (blacklist.empty())
        return;
    auto& db = write_conn();
    std::vector<int64_t> indices;
    indices.reserve(blacklist.size());
    for (auto i : blacklist)
        indices.push_back(as_i64(i));
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

    for (size_t start = 0; start < indices.size(); start += MAX_IN_LIST) {
        auto end = std::min(indices.size(), start + MAX_IN_LIST);
        SQLite::Statement st{
                db.db,
                db::multi_in_query(
                        "UPDATE outputs SET blacklisted = 1 WHERE amount = 0 AND amount_index IN (",
                        end - start,
                        ")")};
        int p = 1;
        for (size_t i = start; i < end; ++i)
            st.bind(p++, indices[i]);
        if (auto updated = st.exec(); static_cast<size_t>(updated) != end - start)
            throw DB_ERROR("Failed to blacklist outputs: {} of {} outputs not found"_format(
                    end - start - updated, end - start));
    }
}

// =========================================================================================
// Key images
// =========================================================================================

void BlockchainSQLiteDB::add_spent_key(const crypto::key_image& k_image) {
    auto& db = write_conn();
    if (db.prepared_maybe_get<int>(
                "SELECT 1 FROM key_images WHERE key_image = ?", as_guts(k_image)))
        throw KEY_IMAGE_EXISTS("Attempting to add spent key image that's already in the db");
    db.prepared_exec(
            "INSERT INTO key_images (key_image, height) "
            "VALUES (?, (SELECT COALESCE(MAX(height) + 1, 0) FROM blocks))",
            as_guts(k_image));
}

void BlockchainSQLiteDB::remove_spent_key(const crypto::key_image& k_image) {
    auto& db = write_conn();
    db.prepared_exec("DELETE FROM key_images WHERE key_image = ?", as_guts(k_image));
}

bool BlockchainSQLiteDB::has_key_image(const crypto::key_image& img) const {
    check_open();
    return m_db->prepared_maybe_get<int>(
                        "SELECT 1 FROM key_images WHERE key_image = ?", as_guts(img))
            .has_value();
}

// =========================================================================================
// Iteration
// =========================================================================================

bool BlockchainSQLiteDB::for_all_key_images(std::function<bool(const crypto::key_image&)> f) const {
    check_open();
    for (auto ki : m_db->prepared_results<blob_guts<crypto::key_image>>(
                 "SELECT key_image FROM key_images ORDER BY key_image"))
        if (!f(ki.value))
            return false;
    return true;
}

bool BlockchainSQLiteDB::for_blocks_range(
        const uint64_t& h1,
        const uint64_t& h2,
        std::function<bool(uint64_t, const crypto::hash&, const cryptonote::block&)> f) const {
    check_open();
    for (uint64_t h = h1; h <= h2; ++h) {
        auto b = get_block_from_height(h);
        crypto::hash hash;
        if (!get_block_hash(b, hash))
            throw DB_ERROR("Failed to get block hash from blob retrieved from the db");
        if (!f(h, hash, b))
            return false;
    }
    return true;
}

bool BlockchainSQLiteDB::for_all_transactions(
        std::function<bool(const crypto::hash&, const cryptonote::transaction&)> f,
        bool pruned) const {
    check_open();
    auto st = m_db->prepared_st("SELECT hash, unprunable, prunable FROM transactions ORDER BY id");
    while (st->executeStep()) {
        crypto::hash hash = blob_guts<crypto::hash>{st->getColumn(0)};
        transaction tx;
        std::string bd = col_str(st->getColumn(1));
        if (pruned) {
            if (!parse_and_validate_tx_base_from_blob(bd, tx))
                throw DB_ERROR("Failed to parse tx from blob retrieved from the db");
        } else {
            auto prunable = st->getColumn(2);
            if (prunable.isNull())
                throw DB_ERROR("Failed to get prunable tx data from the db");
            bd.append(static_cast<const char*>(prunable.getBlob()), prunable.getBytes());
            if (!parse_and_validate_tx_from_blob(bd, tx))
                throw DB_ERROR("Failed to parse tx from blob retrieved from the db");
        }
        if (!f(hash, tx))
            return false;
    }
    return true;
}

bool BlockchainSQLiteDB::for_all_outputs(
        std::function<bool(
                uint64_t amount, const crypto::hash& tx_hash, uint64_t height, size_t tx_idx)> f)
        const {
    check_open();
    for (auto row : m_db->prepared_results<int64_t, blob_guts<crypto::hash>, int64_t, int64_t>(
                 "SELECT o.amount, t.hash, o.height, o.local_index FROM outputs o "
                 "JOIN transactions t ON t.id = o.tx_id ORDER BY o.amount, o.amount_index")) {
        auto& [amount, tx_hash, height, local_index] = row;
        if (!f(as_u64(amount), tx_hash.value, as_u64(height), as_u64(local_index)))
            return false;
    }
    return true;
}

bool BlockchainSQLiteDB::for_all_outputs(
        uint64_t amount, const std::function<bool(uint64_t height)>& f) const {
    check_open();
    for (auto height : m_db->prepared_results<int64_t>(
                 "SELECT height FROM outputs WHERE amount = ? ORDER BY amount_index",
                 as_i64(amount)))
        if (!f(as_u64(height)))
            return false;
    return true;
}

// =========================================================================================
// Max block size
// =========================================================================================

uint64_t BlockchainSQLiteDB::get_max_block_size() {
    check_open();
    auto size = m_db->prepared_maybe_get<int64_t>(
            "SELECT value FROM properties WHERE key = 'max_block_size'");
    return size ? as_u64(*size) : std::numeric_limits<uint64_t>::max();
}

void BlockchainSQLiteDB::add_max_block_size(uint64_t sz) {
    in_write_txn([&](db::Database& db) {
        db.prepared_exec(
                "INSERT INTO properties (key, value) VALUES ('max_block_size', ?) "
                "ON CONFLICT(key) DO UPDATE SET value = MAX(value, excluded.value)",
                as_i64(sz));
    });
}

// =========================================================================================
// Txpool
// =========================================================================================

void BlockchainSQLiteDB::add_txpool_tx(
        const crypto::hash& txid, const std::string& blob, const txpool_tx_meta_t& meta) {
    if (blob.empty())
        throw DB_ERROR("Attempting to add txpool tx with empty blob");
    in_write_txn([&](db::Database& db) {
        db.prepared_exec(
                "INSERT OR REPLACE INTO txpool (hash, blob, {}) VALUES (?, ?, {})"_format(
                        TXPOOL_META_COLUMNS, TXPOOL_META_PLACEHOLDERS),
                as_guts(txid),
                as_blob(blob),
                as_i64(meta.weight),
                as_i64(meta.fee),
                as_i64(meta.receive_time),
                as_i64(meta.last_relayed_time),
                as_i64(meta.max_used_block_height),
                as_guts(meta.max_used_block_id),
                as_i64(meta.last_failed_height),
                as_guts(meta.last_failed_id),
                as_i64(meta.l2_height),
                int{meta.kept_by_block},
                int{meta.relayed},
                int{meta.do_not_relay},
                int{meta.double_spend_seen});
    });
}

void BlockchainSQLiteDB::update_txpool_tx(const crypto::hash& txid, const txpool_tx_meta_t& meta) {
    in_write_txn([&](db::Database& db) {
        auto updated = db.prepared_exec(
                "UPDATE txpool SET ({}) = ({}) WHERE hash = ?"_format(
                        TXPOOL_META_COLUMNS, TXPOOL_META_PLACEHOLDERS),
                as_i64(meta.weight),
                as_i64(meta.fee),
                as_i64(meta.receive_time),
                as_i64(meta.last_relayed_time),
                as_i64(meta.max_used_block_height),
                as_guts(meta.max_used_block_id),
                as_i64(meta.last_failed_height),
                as_guts(meta.last_failed_id),
                as_i64(meta.l2_height),
                int{meta.kept_by_block},
                int{meta.relayed},
                int{meta.do_not_relay},
                int{meta.double_spend_seen},
                as_guts(txid));
        if (updated == 0)
            throw DB_ERROR("Error finding txpool tx meta to update");
    });
}

uint64_t BlockchainSQLiteDB::get_txpool_tx_count(bool include_unrelayed_txes) const {
    check_open();
    return as_u64(m_db->prepared_get<int64_t>(
            include_unrelayed_txes ? "SELECT COUNT(*) FROM txpool"
                                   : "SELECT COUNT(*) FROM txpool WHERE NOT do_not_relay"));
}

bool BlockchainSQLiteDB::txpool_has_tx(const crypto::hash& txid) const {
    check_open();
    return m_db->prepared_maybe_get<int>("SELECT 1 FROM txpool WHERE hash = ?", as_guts(txid))
            .has_value();
}

void BlockchainSQLiteDB::remove_txpool_tx(const crypto::hash& txid) {
    in_write_txn([&](db::Database& db) {
        db.prepared_exec("DELETE FROM txpool WHERE hash = ?", as_guts(txid));
    });
}

bool BlockchainSQLiteDB::get_txpool_tx_meta(const crypto::hash& txid, txpool_tx_meta_t& meta) const {
    check_open();
    auto st = m_db->prepared_bind(
            "SELECT {} FROM txpool WHERE hash = ?"_format(TXPOOL_META_COLUMNS), as_guts(txid));
    if (!st->executeStep())
        return false;
    meta = read_txpool_meta(st, 0);
    return true;
}

bool BlockchainSQLiteDB::get_txpool_tx_blob(const crypto::hash& txid, std::string& bd) const {
    check_open();
    auto st = m_db->prepared_bind("SELECT blob FROM txpool WHERE hash = ?", as_guts(txid));
    if (!st->executeStep())
        return false;
    auto blob = st->getColumn(0);
    if (blob.getBytes() == 0)
        throw DB_ERROR("Error finding txpool tx blob: tx is present, but data is empty");
    bd.assign(col_str(blob));
    return true;
}

std::string BlockchainSQLiteDB::get_txpool_tx_blob(const crypto::hash& txid) const {
    std::string bd;
    if (!get_txpool_tx_blob(txid, bd))
        throw DB_ERROR("Tx not found in txpool: {}"_format(txid));
    return bd;
}

bool BlockchainSQLiteDB::for_all_txpool_txes(
        std::function<bool(const crypto::hash&, const txpool_tx_meta_t&, const std::string*)> f,
        bool include_blob,
        bool include_unrelayed_txes) const {
    check_open();
    auto st = m_db->prepared_st("SELECT hash, {}{} FROM txpool{} ORDER BY hash"_format(
            TXPOOL_META_COLUMNS,
            include_blob ? ", blob" : "",
            include_unrelayed_txes ? "" : " WHERE NOT do_not_relay"));
    std::string bd;
    while (st->executeStep()) {
        crypto::hash txid = blob_guts<crypto::hash>{st->getColumn(0)};
        auto meta = read_txpool_meta(st, 1);
        if (include_blob)
            bd = col_str(st->getColumn(14));
        if (!f(txid, meta, include_blob ? &bd : nullptr))
            return false;
    }
    return true;
}

// =========================================================================================
// Alt blocks
// =========================================================================================

void BlockchainSQLiteDB::add_alt_block(
        const crypto::hash& blkid,
        const alt_block_data_t& data,
        const std::string& blob,
        const std::string* checkpoint) {
    assert(data.checkpointed == (checkpoint != nullptr));
    auto header = convert_block_blob<block_header>(blob);
    std::optional<db::blob_binder> checkpoint_blob;
    if (checkpoint)
        checkpoint_blob = as_blob(*checkpoint);
    in_write_txn([&](db::Database& db) {
        if (db.prepared_maybe_get<int>("SELECT 1 FROM alt_blocks WHERE hash = ?", as_guts(blkid)))
            throw DB_ERROR("Attempting to add alternate block that's already in the db");
        db.prepared_exec(
                "INSERT INTO alt_blocks (hash, height, prev_hash, cumulative_weight, "
                "cumulative_difficulty, coins_generated, blob, checkpoint) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                as_guts(blkid),
                as_i64(data.height),
                as_guts(header.prev_id),
                as_i64(data.cumulative_weight),
                as_i64(data.cumulative_difficulty),
                as_i64(data.already_generated_coins),
                as_blob(blob),
                checkpoint_blob);
    });
}

bool BlockchainSQLiteDB::get_alt_block(
        const crypto::hash& blkid,
        alt_block_data_t* data,
        std::string* blob,
        std::string* checkpoint) const {
    check_open();
    auto st = m_db->prepared_bind(
            "SELECT {}, blob, checkpoint FROM alt_blocks WHERE hash = ?"_format(
                    ALT_BLOCK_DATA_COLUMNS),
            as_guts(blkid));
    if (!st->executeStep())
        return false;
    if (data)
        *data = read_alt_block_data(st, 0);
    if (blob)
        blob->assign(col_str(st->getColumn(5)));
    if (checkpoint && !st->getColumn(6).isNull())
        checkpoint->assign(col_str(st->getColumn(6)));
    return true;
}

void BlockchainSQLiteDB::remove_alt_block(const crypto::hash& blkid) {
    in_write_txn([&](db::Database& db) {
        if (db.prepared_exec("DELETE FROM alt_blocks WHERE hash = ?", as_guts(blkid)) == 0)
            throw DB_ERROR("Error locating alternate block {} in the db"_format(blkid));
    });
}

uint64_t BlockchainSQLiteDB::get_alt_block_count() {
    check_open();
    return as_u64(m_db->prepared_get<int64_t>("SELECT COUNT(*) FROM alt_blocks"));
}

void BlockchainSQLiteDB::drop_alt_blocks() {
    in_write_txn([](db::Database& db) { db.prepared_exec("DELETE FROM alt_blocks"); });
}

bool BlockchainSQLiteDB::for_all_alt_blocks(
        std::function<
                bool(const crypto::hash& blkid,
                     const alt_block_data_t& data,
                     const std::string* block_blob,
                     const std::string* checkpoint_blob)> f,
        bool include_blob) const {
    check_open();
    auto st = m_db->prepared_st("SELECT hash, {}{} FROM alt_blocks ORDER BY hash"_format(
            ALT_BLOCK_DATA_COLUMNS, include_blob ? ", blob, checkpoint" : ""));
    std::string block, checkpoint;
    while (st->executeStep()) {
        crypto::hash blkid = blob_guts<crypto::hash>{st->getColumn(0)};
        auto data = read_alt_block_data(st, 1);
        if (include_blob) {
            block = col_str(st->getColumn(6));
            if (st->getColumn(7).isNull())
                checkpoint.clear();
            else
                checkpoint = col_str(st->getColumn(7));
        }
        if (!f(blkid, data, include_blob ? &block : nullptr, include_blob ? &checkpoint : nullptr))
            return false;
    }
    return true;
}

// =========================================================================================
// M2b-1 -- quorum-signed checkpoints, service-node data + proofs, pruning and histograms.
// These mirror BlockchainLMDB's semantics against the SQLite schema.  The rewards / batching-v2 /
// accrued-history / fallback / rev1 seams remain in db_sqlite.{h,cpp} and are left for M2b-2.
// =========================================================================================

void BlockchainSQLiteDB::update_block_checkpoint(checkpoint_t const& checkpoint) {
    in_write_txn([&](db::Database& db) {
        const auto height = as_i64(checkpoint.height);
        db.prepared_exec(
                "INSERT INTO checkpoints (height, hash, type) VALUES (?, ?, ?) "
                "ON CONFLICT(height) DO UPDATE SET hash = excluded.hash, type = excluded.type",
                height,
                as_guts(checkpoint.block_hash),
                static_cast<int>(checkpoint.type));
        // Replace any existing signatures for this height (the ON CONFLICT above keeps the row, so
        // the ON DELETE CASCADE does not fire; clear them explicitly).
        db.prepared_exec("DELETE FROM checkpoint_signatures WHERE height = ?", height);
        for (const auto& sig : checkpoint.signatures)
            db.prepared_exec(
                    "INSERT INTO checkpoint_signatures (height, voter_index, signature) "
                    "VALUES (?, ?, ?)",
                    height,
                    int{sig.voter_index},
                    as_guts(sig.signature));
    });
}

void BlockchainSQLiteDB::remove_block_checkpoint(uint64_t height) {
    // The ON DELETE CASCADE on checkpoint_signatures removes the signatures with the row.
    in_write_txn([&](db::Database& db) {
        db.prepared_exec("DELETE FROM checkpoints WHERE height = ?", as_i64(height));
    });
}

bool BlockchainSQLiteDB::get_block_checkpoint(uint64_t height, checkpoint_t& checkpoint) const {
    check_open();
    auto row = m_db->prepared_maybe_get<blob_guts<crypto::hash>, int>(
            "SELECT hash, type FROM checkpoints WHERE height = ?", as_i64(height));
    if (!row)
        return false;
    checkpoint = load_checkpoint(
            *m_db,
            as_i64(height),
            std::get<0>(*row).value,
            static_cast<checkpoint_type>(std::get<1>(*row)));
    return true;
}

bool BlockchainSQLiteDB::get_top_checkpoint(checkpoint_t& checkpoint) const {
    check_open();
    auto row = m_db->prepared_maybe_get<int64_t, blob_guts<crypto::hash>, int>(
            "SELECT height, hash, type FROM checkpoints ORDER BY height DESC LIMIT 1");
    if (!row)
        return false;
    auto& [height, hash, type] = *row;
    checkpoint = load_checkpoint(
            *m_db, height, hash.value, static_cast<checkpoint_type>(type));
    return true;
}

// The same results as BlockchainLMDB's cursor walk: start and end are clamped to the stored
// checkpoints' heights, and the result is the checkpoints between them (both inclusive), in order
// from start towards end (descending if start > end), at most num_desired_checkpoints of them.
std::vector<checkpoint_t> BlockchainSQLiteDB::get_checkpoints_range(
        uint64_t start, uint64_t end, size_t num_desired_checkpoints) const {
    check_open();
    std::vector<checkpoint_t> result;

    auto bottom = m_db->prepared_maybe_get<int64_t>(
            "SELECT height FROM checkpoints ORDER BY height LIMIT 1");
    auto top = m_db->prepared_maybe_get<int64_t>(
            "SELECT height FROM checkpoints ORDER BY height DESC LIMIT 1");
    if (!bottom || !top)
        return result;

    start = std::clamp(start, as_u64(*bottom), as_u64(*top));
    end = std::clamp(end, as_u64(*bottom), as_u64(*top));
    const int64_t limit =
            num_desired_checkpoints == GET_ALL_CHECKPOINTS ? -1 : as_i64(num_desired_checkpoints);

    std::vector<std::tuple<int64_t, blob_guts<crypto::hash>, int>> rows;
    for (auto row : m_db->prepared_results<int64_t, blob_guts<crypto::hash>, int>(
                 start <= end ? "SELECT height, hash, type FROM checkpoints "
                                "WHERE height BETWEEN ? AND ? ORDER BY height LIMIT ?"
                              : "SELECT height, hash, type FROM checkpoints "
                                "WHERE height BETWEEN ? AND ? ORDER BY height DESC LIMIT ?",
                 as_i64(std::min(start, end)),
                 as_i64(std::max(start, end)),
                 limit))
        rows.push_back(std::move(row));

    result.reserve(rows.size());
    for (auto& [height, hash, type] : rows)
        result.push_back(
                load_checkpoint(*m_db, height, hash.value, static_cast<checkpoint_type>(type)));
    return result;
}

// Removes every output of the given amount, as BlockchainLMDB::prune_outputs does (used by the
// blockchain_prune_known_spent_data utility, not by normal operation).
void BlockchainSQLiteDB::prune_outputs(uint64_t amount) {
    oxen::log::info(logcat, "Pruning outputs for amount {}", amount);
    in_write_txn([&](db::Database& db) {
        db.prepared_exec("DELETE FROM outputs WHERE amount = ?", as_i64(amount));
    });
}

// This backend is a full (non-pruning) node: it stores every transaction's prunable data and has no
// pruning-seed property, matching how xeqm runs (the --prune-blockchain path is disabled in
// cryptonote_core, and service nodes must retain the full chain).  BlockchainLMDB returns 0 when no
// pruning seed is stored, which is the value the normal block-processing path reads here.
uint32_t BlockchainSQLiteDB::get_blockchain_pruning_seed() const {
    check_open();
    return 0;
}

// Enabling blockchain pruning is not supported by the SQLite backend (it would require a pruning
// stripe scheme this port deliberately does not invent).  Returns false rather than silently
// claiming success; this is only reachable via the explicit prune_blockchain RPC/console command.
bool BlockchainSQLiteDB::prune_blockchain(uint32_t pruning_seed) {
    check_open();
    oxen::log::warning(logcat, "Blockchain pruning is not supported by the SQLite backend");
    return false;
}

// Pruning is not enabled on this backend, so there is nothing to update/check; BlockchainLMDB's
// prune worker likewise returns true ("pruning not enabled, nothing to do") for a non-pruned node.
bool BlockchainSQLiteDB::update_pruning() {
    check_open();
    return true;
}
bool BlockchainSQLiteDB::check_pruning() {
    check_open();
    return true;
}

std::map<uint64_t, std::tuple<uint64_t, uint64_t, uint64_t>>
BlockchainSQLiteDB::get_output_histogram(
        const std::vector<uint64_t>& amounts,
        bool unlocked,
        uint64_t recent_cutoff,
        uint64_t min_count) const {
    check_open();
    std::map<uint64_t, std::tuple<uint64_t, uint64_t, uint64_t>> histogram;

    if (amounts.empty()) {
        // Visits each distinct amount by seeking to the next one, rather than scanning every output
        // (nearly all of which have amount 0).
        for (auto amount = m_db->prepared_maybe_get<int64_t>(
                     "SELECT amount FROM outputs ORDER BY amount LIMIT 1");
             amount;
             amount = m_db->prepared_maybe_get<int64_t>(
                     "SELECT amount FROM outputs WHERE amount > ? ORDER BY amount LIMIT 1",
                     *amount)) {
            if (auto n = num_outputs(*m_db, *amount); n >= min_count)
                histogram[as_u64(*amount)] = {n, 0, 0};
        }
    } else {
        for (auto amount : amounts)
            if (auto n = num_outputs(*m_db, as_i64(amount)); n >= min_count)
                histogram[amount] = {n, 0, 0};
    }

    if (unlocked || recent_cutoff > 0) {
        const uint64_t chain_top = chain_height(*m_db);
        for (auto& [amount, counts] : histogram) {
            auto& [total, unlocked_count, recent] = counts;
            unlocked_count = total;
            for (auto row : m_db->prepared_results<int64_t, int64_t>(
                         "SELECT o.height, b.timestamp FROM outputs o "
                         "JOIN blocks b ON b.height = o.height "
                         "WHERE o.amount = ? ORDER BY o.amount_index DESC",
                         as_i64(amount))) {
                auto& [out_height, timestamp] = row;
                if (as_u64(out_height) + DEFAULT_TX_SPENDABLE_AGE <= chain_top) {
                    if (recent_cutoff == 0 || as_u64(timestamp) < recent_cutoff)
                        break;
                    ++recent;
                } else {
                    --unlocked_count;
                }
            }
        }
    }

    return histogram;
}

// Like BlockchainLMDB, this also counts the first output (if any) above to_height: LMDB's walk over
// the outputs counts each one before checking whether it has passed to_height.
bool BlockchainSQLiteDB::get_output_distribution(
        uint64_t amount,
        uint64_t from_height,
        uint64_t to_height,
        std::vector<uint64_t>& distribution,
        uint64_t& base) const {
    check_open();
    distribution.clear();

    const uint64_t db_height = chain_height(*m_db);
    if (from_height >= db_height)
        return false;
    distribution.resize(db_height - from_height, 0);

    uint64_t below = 0;
    auto add = [&](uint64_t height, uint64_t n) {
        if (height >= from_height)
            distribution[height - from_height] += n;
        else
            below += n;
    };

    const uint64_t last = to_height > 0 ? to_height : db_height;
    below = as_u64(m_db->prepared_get<int64_t>(
            "SELECT COUNT(*) FROM outputs WHERE amount = ? AND height < ? AND height <= ?",
            as_i64(amount),
            as_i64(from_height),
            as_i64(last)));
    for (auto row : m_db->prepared_results<int64_t, int64_t>(
                 "SELECT height, COUNT(*) FROM outputs WHERE amount = ? AND height >= ? "
                 "AND height <= ? GROUP BY height",
                 as_i64(amount),
                 as_i64(from_height),
                 as_i64(last))) {
        auto& [height, n] = row;
        add(as_u64(height), as_u64(n));
    }
    if (to_height > 0)
        if (auto next = m_db->prepared_maybe_get<int64_t>(
                    "SELECT height FROM outputs WHERE amount = ? AND height > ? "
                    "ORDER BY height LIMIT 1",
                    as_i64(amount),
                    as_i64(to_height)))
            add(as_u64(*next), 1);

    distribution[0] += below;
    for (size_t n = 1; n < distribution.size(); ++n)
        distribution[n] += distribution[n - 1];
    base = 0;
    return true;
}

// Service-node state and proofs are stored as opaque blobs, exactly as BlockchainLMDB stores them.

void BlockchainSQLiteDB::set_service_node_data(const std::string& data, bool long_term) {
    const int64_t key = long_term ? SERVICE_NODE_BLOB_LONG_TERM_KEY : SERVICE_NODE_BLOB_SHORT_TERM_KEY;
    in_write_txn([&](db::Database& db) {
        db.prepared_exec(
                "INSERT INTO service_node_data (id, data) VALUES (?, ?) "
                "ON CONFLICT(id) DO UPDATE SET data = excluded.data",
                key,
                as_blob(data));
    });
}

bool BlockchainSQLiteDB::get_service_node_data(std::string& data, bool long_term) const {
    check_open();
    const int64_t key = long_term ? SERVICE_NODE_BLOB_LONG_TERM_KEY : SERVICE_NODE_BLOB_SHORT_TERM_KEY;
    auto st = m_db->prepared_bind("SELECT data FROM service_node_data WHERE id = ?", key);
    if (!st->executeStep())
        return false;
    data = col_str(st->getColumn(0));
    return true;
}

void BlockchainSQLiteDB::clear_service_node_data() {
    in_write_txn([](db::Database& db) { db.prepared_exec("DELETE FROM service_node_data"); });
}

bool BlockchainSQLiteDB::get_service_node_proof(
        const crypto::public_key& pubkey, service_nodes::proof_info& proof) const {
    check_open();
    auto st = m_db->prepared_bind(
            "SELECT proof FROM service_node_proofs WHERE pubkey = ?", as_guts(pubkey));
    if (!st->executeStep())
        return false;
    load_service_node_proof(col_str(st->getColumn(0)), proof);
    return true;
}

std::unordered_map<crypto::public_key, service_nodes::proof_info>
BlockchainSQLiteDB::get_all_service_node_proofs() const {
    check_open();
    std::unordered_map<crypto::public_key, service_nodes::proof_info> result;
    auto st = m_db->prepared_st("SELECT pubkey, proof FROM service_node_proofs");
    while (st->executeStep()) {
        crypto::public_key pubkey = blob_guts<crypto::public_key>{st->getColumn(0)};
        service_nodes::proof_info info;
        load_service_node_proof(col_str(st->getColumn(1)), info);
        result.emplace(pubkey, std::move(info));
    }
    return result;
}

void BlockchainSQLiteDB::set_service_node_proof(
        const crypto::public_key& pubkey, const service_nodes::proof_info& proof) {
    service_node_proof_serialized data{proof};
    in_write_txn([&](db::Database& db) {
        db.prepared_exec(
                "INSERT INTO service_node_proofs (pubkey, proof) VALUES (?, ?) "
                "ON CONFLICT(pubkey) DO UPDATE SET proof = excluded.proof",
                as_guts(pubkey),
                as_blob(std::string_view{reinterpret_cast<const char*>(&data), sizeof(data)}));
    });
}

bool BlockchainSQLiteDB::remove_service_node_proof(const crypto::public_key& pubkey) {
    bool removed = false;
    in_write_txn([&](db::Database& db) {
        removed = db.prepared_exec(
                          "DELETE FROM service_node_proofs WHERE pubkey = ?", as_guts(pubkey)) > 0;
    });
    return removed;
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#undef M2_STUB

}  // namespace cryptonote
