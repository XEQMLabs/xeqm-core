// Copyright (c) 2021, The Oxen Project
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

#include "db_sqlite.h"

#include <common/exception.h>
#include <common/guts.h>
#include <cryptonote_basic/hardfork.h>
#include <cryptonote_config.h>
#include <cryptonote_core/blockchain.h>
#include <cryptonote_core/cryptonote_tx_utils.h>
#include <fmt/core.h>
#include <sodium.h>
#include <sqlite3.h>
#include <tracy/Tracy.hpp>

#include <cassert>

namespace cryptonote {

static auto logcat = log::Cat("blockchain.db.sqlite");

struct delayed_payment
{
    BlockchainSQLite::exit_stake exit;
    uint64_t payout_height;
};

std::map<uint64_t /*height*/, block_payments> batched_payments_accrued_staging;
std::map<uint64_t /*height*/, std::vector<delayed_payment>> delayed_payments_staging;
bool commit_on_block_add = true;

BlockchainSQLite::BlockchainSQLite(
        cryptonote::network_type nettype, std::filesystem::path db_path) :
        db::Database(db_path, ""), m_nettype(nettype) {
    log::trace(logcat, "BlockchainDB_SQLITE::{}", __func__);
    height = 0;

    if (!table_exists("batched_payments_accrued"))
        create_schema();
    upgrade_schema();

    height = prepared_get<int64_t>("SELECT height FROM batch_db_info");
    commit_height = height;

    uint64_t row_count =
            batch_payments_accrued_row_count(PaymentTableType::Nil, /*height*/ std::nullopt);
    uint64_t recent_count =
            batch_payments_accrued_row_count(PaymentTableType::Recent, /*height*/ std::nullopt);
    uint64_t recent_min_height =
            prepared_get<int>("SELECT MIN(height) FROM batched_payments_accrued_recent");
    uint64_t recent_max_height =
            prepared_get<int>("SELECT MAX(height) FROM batched_payments_accrued_recent");

    uint64_t archive_count =
            batch_payments_accrued_row_count(PaymentTableType::Archive, /*height*/ std::nullopt);
    uint64_t archive_min_height =
            prepared_get<int>("SELECT MIN(height) FROM batched_payments_accrued_archive");
    uint64_t archive_max_height =
            prepared_get<int>("SELECT MAX(height) FROM batched_payments_accrued_archive");

    // NOTE: Populate in-memory stores
    {
        // NOTE: Batched payments
        auto batched_query = prepared_results<std::string, int64_t>("SELECT address, amount FROM batched_payments_accrued");
        block_payments& payments = batched_payments_accrued_staging[height];
        for (auto [addr_str, amt] : batched_query) {
            std::variant<eth::address, cryptonote::account_public_address> addr;
            if (addr_str.size() == (2 + (sizeof(eth::address) * 2))) {
                addr = tools::make_from_hex_guts<eth::address>(addr_str);
            } else {
                cryptonote::address_parse_info parse_info = {};
                [[maybe_unused]] bool ok =
                        cryptonote::get_account_address_from_str(parse_info, m_nettype, addr_str);
                assert(ok);
                addr = parse_info.address;
            }

            payments[addr] = cryptonote::reward_money::db_amount(amt).to_db();
        }

        // NOTE: Delayed payments
        auto delayed_query = prepared_results<std::string, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t>(
                "SELECT eth_address, amount, payout_height, height, block_height, block_tx_index, "
                "contributor_index FROM delayed_payments");
        for (auto item : delayed_query) {
            std::string addr_str = std::get<0>(item);
            int64_t amount = std::get<1>(item);
            int64_t payout_height = std::get<2>(item);
            int64_t height = std::get<3>(item);
            int64_t block_height = std::get<4>(item);
            int64_t block_tx_index = std::get<5>(item);
            int64_t contributor_index = std::get<6>(item);

            exit_stake stake = {
                .addr = tools::make_from_hex_guts<eth::address>(addr_str),
                .amount = cryptonote::reward_money::db_amount(amount),
                .block_height = static_cast<uint32_t>(block_height),
                .tx_index = static_cast<uint32_t>(block_tx_index),
                .contributor_index = static_cast<uint32_t>(contributor_index),
            };

            delayed_payments_staging[block_height].emplace_back(stake, payout_height);
        }
    }

    log::info(
            globallogcat,
            "{} rows, {} recent [blks {}-{}], {} historical [blks {}-{}] loaded @ height: {}",
            row_count,
            recent_count,
            recent_min_height,
            recent_max_height,
            archive_count,
            archive_min_height,
            archive_max_height,
            height);
}

void BlockchainSQLite::create_schema() {
    log::trace(logcat, "BlockchainDB_SQLITE::{}", __func__);

    auto& netconf = cryptonote::get_config(m_nettype);

    db.exec(R"(CREATE TABLE IF NOT EXISTS batched_payments_accrued(
                 address VARCHAR NOT NULL,
                 amount BIGINT NOT NULL,
                 payout_offset INTEGER NOT NULL,
                 PRIMARY KEY(address),
                 CHECK(amount >= 0)
               );

               CREATE INDEX IF NOT EXISTS batched_payments_accrued_payout_offset_idx ON batched_payments_accrued(payout_offset);

               -- For pre-ETH hardfork. Oxen SN's were paid and the amount paid was subtracted
               -- from the accumulated amount in the DB. After the ETH hardfork the DB tracks
               -- the lifetime rewards and instead the smart contract tracks how much has been paid
               -- out. The delta in how much the DB has allocated and how much the smart contract
               -- has paid is the amount owed.
               --
               -- In other words after hardforking, rewards amounts are strictly accumulative which
               -- means this condition will never trigger.
               CREATE TRIGGER IF NOT EXISTS batch_payments_delete_empty AFTER UPDATE ON batched_payments_accrued
               FOR EACH ROW WHEN NEW.amount = 0 BEGIN
                   DELETE FROM batched_payments_accrued WHERE address = NEW.address;
               END;

               CREATE TABLE IF NOT EXISTS batch_db_info(height BIGINT NOT NULL);
               INSERT INTO  batch_db_info(height) VALUES(0);)");

    log::debug(logcat, "Database setup complete");
}

bool BlockchainSQLite::table_exists(const std::string& table_name) {
    return prepared_get<int>(
            "SELECT EXISTS(SELECT 1 FROM sqlite_master WHERE type='table' AND name=?)", table_name);
}

bool BlockchainSQLite::trigger_exists(const std::string& trigger_name) {
    return prepared_get<int>(
            "SELECT EXISTS(SELECT 1 FROM sqlite_master WHERE type='trigger' AND name=?)",
            trigger_name);
}

void BlockchainSQLite::upgrade_schema() {
    bool have_offset = false;
    SQLite::Statement msg_cols{db, "PRAGMA main.table_info(batched_payments_accrued)"};
    while (msg_cols.executeStep()) {
        auto [cid, name] = db::get<int64_t, std::string>(msg_cols);
        if (name == "payout_offset")
            have_offset = true;
    }

    SQLite::Transaction transaction{db, SQLite::TransactionBehavior::DEFERRED};
    // NOTE: Rename 'batched_payments_accrued_archive' 'archive_height' column to 'height'. This
    // unifies the height label across the batch payment, recent and archive table making querying
    // from them require less code.
    // TODO: After HF20 we can remove this code as everyone will have upgraded their schema.
    {
        bool has_deprecated_archive_height_column = false;
        SQLite::Statement msg_cols{db, "PRAGMA main.table_info(batched_payments_accrued_archive)"};
        while (msg_cols.executeStep()) {
            auto [cid, name] = db::get<int64_t, std::string>(msg_cols);
            if (name == "archive_height") {
                has_deprecated_archive_height_column = true;
                break;
            }
        }

        if (has_deprecated_archive_height_column)
            db.exec("ALTER TABLE batched_payments_accrued_archive RENAME COLUMN archive_height to "
                    "height;\n");
    }

    if (!have_offset) {
        log::debug(logcat, "Adding payout_offset to batching db");
        auto& netconf = get_config(m_nettype);

        db.exec(R"(ALTER TABLE batched_payments_accrued ADD COLUMN payout_offset INTEGER NOT NULL DEFAULT -1;
                   CREATE INDEX batched_payments_accrued_payout_offset_idx ON batched_payments_accrued(payout_offset);)");

        auto st = prepared_st(
                "UPDATE batched_payments_accrued SET payout_offset = ? WHERE address = ?");
        for (const auto& address : prepared_results<std::string>("SELECT address from "
                                                                 "batched_payments_accrued")) {
            cryptonote::address_parse_info addr_info{};
            cryptonote::get_account_address_from_str(addr_info, m_nettype, address);
            auto offset = static_cast<int>(addr_info.address.modulus(netconf.BATCHING_INTERVAL));
            exec_query(st, offset, address);
            st->reset();
        }

        auto count = prepared_get<int>(
                "SELECT COUNT(*) FROM batched_payments_accrued WHERE payout_offset NOT BETWEEN 0 "
                "AND ?",
                static_cast<int>(netconf.BATCHING_INTERVAL));

        if (count != 0) {
            constexpr auto error =
                    "Batching db update to add offsets failed: not all addresses were converted";
            log::error(logcat, error);
            throw oxen::traced<std::runtime_error>{error};
        }
    }

    std::string_view const DELAYED_PAYMENTS_SCHEMA = R"(
          eth_address       VARCHAR NOT NULL,
          amount            BIGINT  NOT NULL,
          payout_height     BIGINT  NOT NULL, -- Height that the payment was given to 'eth_address' and removed from this table
          height            INT     NOT NULL, -- Height that the payment was added to the DB
          block_height      INT     NOT NULL, -- Height that the TX with the SN exit event was mined in
          block_tx_index    INT     NOT NULL, -- Index of the TX in the block at 'block_height'
          contributor_index INT     NOT NULL, -- Index of the contributor in a multi-contributor SN's stake
          UNIQUE            (block_height, block_tx_index, contributor_index)
          CHECK(amount            >= 0)
          CHECK(payout_height     >= 0)
          CHECK(height            >= 0)
          CHECK(block_height      >= 0)
          CHECK(block_tx_index    >= 0)
          CHECK(contributor_index >= 0)
   )";

    // NOTE: Stores time-locked payments that will be paid out once
    // 'payout_height' is met. This is typically then for when SN's exit the
    // network, their stake is locked for X amount of time before the network
    // merges these payments into 'batch_payments_accrued`.
    //
    // The network will then uniformly agree to sign a signature to permit the
    // address to withdraw those tokens from the smart contract.
    if (!table_exists("delayed_payments")) {
        log::debug(logcat, "Adding delayed payments table to batching db");
        db.exec(R"(
        CREATE TABLE delayed_payments(
          {}
        );
        CREATE INDEX delayed_payments_payout_height_idx ON delayed_payments(payout_height);
        )"_format(DELAYED_PAYMENTS_SCHEMA));
    }

    // NOTE: The archive table stores copies of 'delayed_payments' rows at
    // intervals of 'HISTORY_ARCHIVE_INTERVAL' blocks in a rolling window of
    // 'HISTORY_ARCHIVE_KEEP_WINDOW'
    if (!table_exists("delayed_payments_archive")) {
        log::debug(logcat, "Adding delayed payments (archive) to batching DB");
        auto& netconf = get_config(m_nettype);
        db.exec(R"(
        -- Create archive table that stores the delayed payment rows every 'HISTORY_ARCHIVE_INTERVAL'
        -- blocks
        CREATE TABLE delayed_payments_archive(
          {}
        );
        CREATE INDEX delayed_payments_archive_height_idx ON delayed_payments_archive(height);
        )"_format(DELAYED_PAYMENTS_SCHEMA));
    }

    // NOTE: The recent table stores copies of 'batch_payments_accrued' rows at
    // each height in a rolling window consisting of the past 'HISTORY_RECENT_KEEP_WINDOW' heights.
    if (!table_exists("delayed_payments_recent")) {
        log::debug(logcat, "Adding delayed payments (recent) to batching DB");
        auto& netconf = get_config(m_nettype);
        db.exec(R"(
        CREATE TABLE delayed_payments_recent(
          {}
        );
        CREATE INDEX delayed_payments_recent_height_idx ON delayed_payments_recent(height);
        )"_format(DELAYED_PAYMENTS_SCHEMA));
    }

    // NOTE: The archive table stores copies of 'batch_payments_accrued' rows at
    // intervals of 'HISTORY_ARCHIVE_INTERVAL' blocks in a rolling window of
    // 'HISTORY_ARCHIVE_KEEP_WINDOW'
    if (!table_exists("batched_payments_accrued_archive")) {
        log::debug(logcat, "Adding archiving to batching db");
        auto& netconf = get_config(m_nettype);
        db.exec(R"(
        -- Create archive table that stores the current accrued rows every 'HISTORY_ARCHIVE_INTERVAL'
        -- blocks
        CREATE TABLE batched_payments_accrued_archive(
          address VARCHAR NOT NULL,
          amount BIGINT NOT NULL,
          payout_offset INTEGER NOT NULL,
          height BIGINT NOT NULL, -- Height that the row was generated on
          CHECK(amount >= 0),
          CHECK(height >= 0)
        );

        CREATE INDEX batched_payments_accrued_archive_height_idx ON batched_payments_accrued_archive(height);
        )");
    }

    // NOTE: The recent table stores copies of 'batch_payments_accrued' rows at
    // each height in a rolling window consisting of the past 'HISTORY_RECENT_KEEP_WINDOW' heights.
    if (!table_exists("batched_payments_accrued_recent")) {
        // This table is effectively identical to the above, but because we insert and delete on it
        // for *every* height, partitioning the recent rows in a separate table makes deletions of
        // stale rows a bit faster because we can use a simple `height < x` query rather than a much
        // more complicated (and much less indexable) condition that also worries about not deleting
        // long-term archive rows.
        log::debug(logcat, "Adding recent rewards to batching db");
        auto& netconf = get_config(m_nettype);
        db.exec(R"(
        CREATE TABLE batched_payments_accrued_recent(
          address VARCHAR NOT NULL,
          amount BIGINT NOT NULL,
          payout_offset INTEGER NOT NULL,
          height BIGINT NOT NULL,
          CHECK(amount >= 0),
          CHECK(height >= 0)
        );

        CREATE INDEX batched_payments_accrued_recent_height_idx ON batched_payments_accrued_recent(height);
        )");
    }

    // TODO: Code block can be removed after HF20 on mainnet as everyone's
    // schema's will have been upgraded. Cut and paste into the SQL schema
    // creation code after all tables are made.
    //
    // - make_recent
    // - make_archive
    // - clear_recent_and_archive
    // - delayed_payments_prune
    //
    // Triggers to maintain the table when blocks are added or the blockchain
    // detaches with the following format specifiers. Note that _order_ of the
    // triggers is important as the operations has side effects on tables.
    //
    // {0} => Recent window    => HISTORY_RECENT_KEEP_WINDOW
    // {1} => Archive interval => HISTORY_ARCHIVE_INTERVAL
    // {2} => Archive window   => HISTORY_ARCHIVE_KEEP_WINDOW
    //
    {
        auto& netconf = get_config(m_nettype);
        db.exec(
                R"(
        -- Saves the current payments into their recent table(s) for the current height
        DROP   TRIGGER IF EXISTS make_recent;
        CREATE TRIGGER           make_recent AFTER UPDATE ON batch_db_info
        FOR EACH ROW WHEN NEW.height > OLD.height BEGIN
            -- Batched payments
            INSERT INTO batched_payments_accrued_recent SELECT *, NEW.height FROM  batched_payments_accrued;
            DELETE FROM batched_payments_accrued_recent                      WHERE height < (NEW.height - {0});

            -- Delayed payments
            INSERT INTO delayed_payments_recent SELECT *                     FROM  delayed_payments WHERE height == NEW.height;
            DELETE FROM delayed_payments_recent                              WHERE height < (NEW.height - {0});

        END;

        -- Keep a copy of all the rows for payments for this height if it's on an archival
        -- interval. It allows the DB to gracefully handle block re-orgs without having to
        -- recalculate from scratch.
        --
        -- We archive state at every 'HISTORY_ARCHIVE_INTERVAL' height and we prune the stored
        -- archive to encompass the past 'HISTORY_ARCHIVE_WINDOW' blocks worth of history.
        --
        -- When pruning we floor to the closest interval to make the SQL table match the equivalent
        -- pruning math ('cull_height') in the SNL at 'process_block()'.
        DROP   TRIGGER IF EXISTS make_archive;
        CREATE TRIGGER           make_archive AFTER UPDATE ON batch_db_info
        FOR EACH ROW WHEN (NEW.height % {1}) = 0 AND NEW.height > OLD.height BEGIN

            -- Batch payments
            INSERT INTO batched_payments_accrued_archive SELECT *, NEW.height FROM  batched_payments_accrued;
            DELETE FROM batched_payments_accrued_archive                      WHERE height < (NEW.height - {2});

            -- Delayed payments
            INSERT INTO delayed_payments_archive SELECT *                     FROM  delayed_payments;
            DELETE FROM delayed_payments_archive                              WHERE height < (NEW.height - {2});

        END;

        -- On re-org to a lower height, delete all recent rows that are newer
        -- than the re-org height in all the tables
        --
        -- We rename the trigger to be more apt for its new role of handling
        -- both archive and recent tables.
        DROP   TRIGGER IF EXISTS clear_recent;
        DROP   TRIGGER IF EXISTS clear_recent_and_archive;
        CREATE TRIGGER           clear_recent_and_archive AFTER UPDATE ON batch_db_info
        FOR EACH ROW WHEN NEW.height < OLD.height BEGIN

            -- Batched payments
            DELETE FROM batched_payments_accrued_recent  WHERE height > NEW.height;
            DELETE FROM batched_payments_accrued_archive WHERE height > NEW.height;

            -- Delayed payments
            DELETE FROM delayed_payments_recent          WHERE height > NEW.height;
            DELETE FROM delayed_payments_archive         WHERE height > NEW.height;

        END;

        -- Delete processed delayed payments from the DB when a block is added to the blockchain
        DROP TRIGGER IF EXISTS delayed_payments_prune;
        CREATE TRIGGER         delayed_payments_prune AFTER UPDATE ON batch_db_info
        FOR EACH ROW WHEN NEW.height > OLD.height BEGIN
            DELETE FROM delayed_payments WHERE payout_height <= NEW.height;
        END;
        )"_format(netconf.HISTORY_RECENT_KEEP_WINDOW,
                  netconf.HISTORY_ARCHIVE_INTERVAL,
                  netconf.HISTORY_ARCHIVE_KEEP_WINDOW));
    }

    // NOTE: Remove deprecated tables, this can be removed post HF21 as everyone
    // will have upgraded.
    db.exec(R"(DROP TABLE IF EXISTS batched_payments_accrued_raw;
               DROP VIEW  IF EXISTS batched_payments_accrued_paid;)");

    transaction.commit();
}

void BlockchainSQLite::reset_database() {
    log::trace(logcat, "BlockchainDB_SQLITE::{}", __func__);

    db.exec(R"(
      DROP TABLE IF EXISTS delayed_payments;
      DROP TABLE IF EXISTS delayed_payments_archive;
      DROP TABLE IF EXISTS delayed_payments_recent;

      DROP TABLE IF EXISTS batched_payments_accrued;
      DROP TABLE IF EXISTS batched_payments_accrued_archive;
      DROP TABLE IF EXISTS batched_payments_accrued_recent;

      DROP TABLE IF EXISTS batch_db_info;
    )");

    create_schema();
    upgrade_schema();
    update_height(0, /*commit*/ false);
    batched_payments_accrued_staging.clear();
    delayed_payments_staging.clear();
    log::debug(logcat, "Database reset complete");
}

void BlockchainSQLite::update_height(uint64_t new_height, bool commit) {
    ZoneScoped;
    log::trace(
            logcat,
            "BlockchainDB_SQLITE::{} Changing to height: {}, prev: {}",
            __func__,
            new_height,
            height);
    height = new_height;
    if (commit)
        prepared_exec("UPDATE batch_db_info SET height = ?", static_cast<int64_t>(height));
}

void BlockchainSQLite::blockchain_detached(PaymentTableType history, uint64_t new_height) {
    ZoneScoped;
    const auto& netconf = get_config(m_nettype);

    // NOTE: Execute detach
    std::string detach_label = "";
    int rows_restored = 0;
    int rows_removed = batch_payments_accrued_row_count(PaymentTableType::Nil, std::nullopt);
    switch (history) {
        case PaymentTableType::Nil: {
            reset_database();
            detach_label = " (via reset)";
        } break;

        default: {
            std::string batched_payments_history_table = "batched_payments_accrued_{}"_format(
                    history == PaymentTableType::Archive ? "archive" : "recent");
            rows_restored = batch_payments_accrued_row_count(history, new_height);

            db.exec(R"(DELETE FROM batched_payments_accrued;
                       INSERT INTO batched_payments_accrued
                       SELECT address, amount, payout_offset
                       FROM {1} WHERE height = {0};
              )"_format(new_height, batched_payments_history_table));

            std::string delayed_payments_history_table = "delayed_payments_{}"_format(
                    history == PaymentTableType::Archive ? "archive" : "recent");
            db.exec(R"(DELETE FROM delayed_payments;
                       INSERT INTO delayed_payments
                       SELECT *
                       FROM {1} WHERE {0} >= height AND {0} <= payout_height;
              )"_format(new_height, delayed_payments_history_table));
        } break;
    }

    // NOTE: Detach the staging cache
    auto del_it = batched_payments_accrued_staging.upper_bound(new_height);
    batched_payments_accrued_staging.erase(del_it, batched_payments_accrued_staging.end());

    // NOTE: Apply new height
    update_height(new_height, true);
    log::debug(
            logcat,
            "Detach request for SQL @ {} executed to {}{} (-{} rows deleted, +{} restored)",
            new_height,
            height,
            detach_label,
            rows_removed,
            rows_restored);
}

// Must be called with the address_str_cache_mutex held!
std::string BlockchainSQLite::get_address_str(const cryptonote::batch_sn_payment& addr) {
    ZoneScoped;
    auto& address_str = address_str_cache[addr.address_info.address];
    if (address_str.empty())
        address_str =
                cryptonote::get_account_address_as_str(m_nettype, 0, addr.address_info.address);
    return address_str;
}
std::pair<int, std::string> BlockchainSQLite::get_address_str(
        const std::variant<eth::address, cryptonote::account_public_address>& addr,
        uint64_t batching_interval) {
    ZoneScoped;
    std::pair<int, std::string> result;
    auto& [offset, address_str] = result;
    if (auto* eth_addr = std::get_if<eth::address>(&addr)) {
        offset = 0;  // ignored for SENT
        address_str = "0x{:x}"_format(*eth_addr);
    } else {
        auto* oxen_addr = std::get_if<cryptonote::account_public_address>(&addr);
        assert(oxen_addr);
        offset = static_cast<int>(oxen_addr->modulus(batching_interval));
        auto& cached = address_str_cache[*oxen_addr];
        if (cached.empty())
            cached = cryptonote::get_account_address_as_str(m_nettype, 0, *oxen_addr);
        address_str = cached;
    }
    return result;
}

void BlockchainSQLite::add_sn_rewards(const block_payments& payments) {
    ZoneScoped;
    log::trace(logcat, "BlockchainDB_SQLITE::{}", __func__);
    batched_payments_accrued_staging[height + 1] = payments;
}

bool BlockchainSQLite::commit()
{
    const auto& netconf = get_config(m_nettype);
    auto sql_insert_batched_payment = prepared_st(
            "INSERT INTO batched_payments_accrued (address, payout_offset, amount) VALUES (?, ?, ?)"
            " ON CONFLICT (address) DO UPDATE SET amount = amount + excluded.amount");

    auto sql_insert_delayed_payment = prepared_st(
            "INSERT INTO delayed_payments (eth_address, amount, payout_height, height, "
            "block_height, block_tx_index, contributor_index) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)");

    // NOTE This operation is only relevant pre-eth-bls transition
    auto sql_update_paid = prepared_st(
            "INSERT INTO batched_payments_paid (address, amount, height_paid) VALUES (?,?,?)");

    try {
        SQLite::Transaction transaction{db, SQLite::TransactionBehavior::IMMEDIATE};
        std::lock_guard<std::mutex> a_s_lock{address_str_cache_mutex};
        for (; commit_height < height; commit_height++) {
            uint64_t write_height = commit_height + 1;
            const auto& batched_payments_it = batched_payments_accrued_staging.find(write_height);
            assert(batched_payments_it != batched_payments_accrued_staging.end());

            // NOTE: Add block payments
            for (const auto& [addr, amt] : batched_payments_it->second /*block_payments*/) {
                auto [offset, address_str] = get_address_str(addr, netconf.BATCHING_INTERVAL);
                auto amount = static_cast<int64_t>(amt);
                log::trace(
                        logcat,
                        "Adding record for SN reward contributor {} to database with amount {}",
                        address_str,
                        amt);
                db::exec_query(sql_insert_batched_payment, address_str, offset, amount);
                sql_insert_batched_payment->reset();
            }

            // NOTE: Add delayed payments
            const auto& delayed_payments_it = delayed_payments_staging.find(write_height);
            std::span<const delayed_payment> delayed_payments = {};
            if (delayed_payments_it != delayed_payments_staging.end())
                delayed_payments = delayed_payments_it->second;

            for (const auto& payment : delayed_payments) {
                const auto amount = static_cast<int64_t>(payment.exit.amount.to_db());
                const auto eth_address = "0x{:x}"_format(payment.exit.addr);
                log::trace(
                        logcat,
                        "Adding delayed payment for SN reward contributor {} to database with "
                        "amount "
                        "{}; height {}; payout height {}",
                        eth_address,
                        amount,
                        write_height,
                        payment.payout_height);
                db::exec_query(
                        sql_insert_delayed_payment,
                        eth_address,
                        amount,
                        static_cast<int64_t>(payment.payout_height),
                        static_cast<int64_t>(write_height),
                        payment.exit.block_height,
                        payment.exit.tx_index,
                        payment.exit.contributor_index);
                sql_insert_delayed_payment->reset();
            }

            update_height(write_height, true /*commit*/);
        }
        transaction.commit();
    } catch (std::exception& e) {
        log::error(logcat, "Error adding reward payments: {}", e.what());
        return false;
    }

    return true;
}

size_t BlockchainSQLite::batch_payments_accrued_row_count(
        PaymentTableType type, std::optional<uint64_t> height) {
    ZoneScoped;
    size_t result = 0;
    switch (type) {
        case PaymentTableType::Nil:
            result = prepared_get<int>("SELECT COUNT(*) FROM batched_payments_accrued");
            break;

        case PaymentTableType::Archive:
            if (height) {
                result = prepared_get<int>(
                        "SELECT COUNT(*) FROM batched_payments_accrued_archive WHERE height = ?",
                        static_cast<int64_t>(*height));
            } else {
                result = prepared_get<int>("SELECT COUNT(*) FROM batched_payments_accrued_archive");
            }
            break;

        case PaymentTableType::Recent:
            if (height) {
                result = prepared_get<int>(
                        "SELECT COUNT(*) FROM batched_payments_accrued_recent WHERE height = ?",
                        static_cast<int64_t>(*height));
            } else {
                result = prepared_get<int>("SELECT COUNT(*) FROM batched_payments_accrued_recent");
            }
            break;
    }

    return result;
}

std::vector<cryptonote::batch_sn_payment> BlockchainSQLite::get_pre_eth_bls_sn_payments(uint64_t block_height) {
    ZoneScoped;
    log::trace(logcat, "BlockchainDB_SQLITE::{}", __func__);

    // <= here because we might have crap in the db that we don't clear until we actually add the HF
    // block later on.  (This is a pretty slim edge case that happened on devnet and is probably
    // virtually impossible on mainnet).
    if (m_nettype != cryptonote::network_type::FAKECHAIN &&
        block_height <=
                cryptonote::hard_fork_begins(m_nettype, hf::hf19_reward_batching).value_or(0))
        return {};

    std::vector<cryptonote::batch_sn_payment> result;
    const auto& staging_it = batched_payments_accrued_staging.find(block_height);
    if (staging_it == batched_payments_accrued_staging.end())
        return result;

    const block_payments& payments = staging_it->second;
    result.reserve(payments.size());

    for (const auto& it : payments) {
        auto& p = result.emplace_back();
        // NOTE: Clamp to atomic OXEN
        p.amount = reward_money::db_amount(it.second / BATCH_REWARD_FACTOR * BATCH_REWARD_FACTOR);
        p.address_info.address = std::get<cryptonote::account_public_address>(it.first);
    }

    // NOTE: Sort in ascending as required for consensus
    std::lock_guard address_str_lock{address_str_cache_mutex};
    std::sort(
            result.begin(),
            result.end(),
            [this](const cryptonote::batch_sn_payment& lhs,
                   const cryptonote::batch_sn_payment& rhs) {
                std::string lhs_str = get_address_str(lhs);
                std::string rhs_str = get_address_str(rhs);
                bool result = lhs_str < rhs_str;
                return result;
            });

    return result;
}

std::pair<uint64_t, uint64_t> BlockchainSQLite::get_accrued_rewards(
        const std::variant<eth::address, account_public_address>& address) {
    // NOTE: Guaranteed to give a number as we query the top height. The only time a nullopt is
    // returned is if we are attempting to query historical data.
    std::optional<uint64_t> rewards = get_accrued_rewards(address, height);
    return {height, *rewards};
}

std::optional<uint64_t> BlockchainSQLite::get_accrued_rewards(
        const std::variant<eth::address, account_public_address>& address, uint64_t at_height) {

    // NOTE: Generate the address string for trace logs
    if (oxen::log::get_level(logcat) <= oxen::log::Level::trace) {
        std::lock_guard address_str_lock{address_str_cache_mutex};
        std::string address_string = get_address_str(address, 0).second;
        log::trace(logcat, "BlockchainDB_SQLITE {} for {}", __func__, address_string);
    }

    // NOTE: Retrieve
    uint64_t result = 0;
    const auto& staging_it = batched_payments_accrued_staging.find(at_height);
    if (staging_it == batched_payments_accrued_staging.end()) {
        // NOTE: Fetch from DB
        std::unique_lock address_str_lock{address_str_cache_mutex};
        std::string address_string = get_address_str(address, 0).second;
        address_str_lock.unlock();

        auto amount = prepared_maybe_get<int64_t>(
                "SELECT amount FROM batched_payments_accrued_recent WHERE address = ? AND "
                "height = ?",
                address_string,
                static_cast<int64_t>(height));

        if (amount) {
            result = cryptonote::reward_money::db_amount(amount.value_or(0)).to_coin();
        } else {
            // No rewards found; check to see if we actually have any recent records for that
            // height and if not, return a "don't know" nullopt value.  Otherwise we fall
            // through and return an authoritive 0 value.
            auto min_height = prepared_get<int64_t>(
                    "SELECT COALESCE(MIN(height), 0) FROM batched_payments_accrued_recent");
            if (height < static_cast<uint64_t>(min_height))
                return std::nullopt;
        }
    } else {
        const block_payments& payments = staging_it->second;
        auto it = payments.find(address);
        if (it != payments.end())
            result = cryptonote::reward_money::db_amount(it->second).to_coin();
    }

    return result;
}

std::pair<std::vector<std::string>, std::vector<uint64_t>>
BlockchainSQLite::get_all_accrued_rewards() {
    log::trace(logcat, "BlockchainDB_SQLITE::{}", __func__);

    std::pair<std::vector<std::string>, std::vector<uint64_t>> result;
    auto& [addresses, amounts] = result;

    const auto& staging_it = batched_payments_accrued_staging.find(height);
    if (staging_it != batched_payments_accrued_staging.end()) {
        const auto& netconf = get_config(m_nettype);
        const block_payments& payments = staging_it->second;
        addresses.reserve(payments.size());
        amounts.reserve(payments.size());
        std::lock_guard address_str_lock{address_str_cache_mutex};
        for (const auto& it : payments) {
            addresses.push_back(get_address_str(it.first, netconf.BATCHING_INTERVAL).second);
            amounts.push_back(cryptonote::reward_money::db_amount(it.second).to_coin());
        }
    }

    return result;
}

void BlockchainSQLite::add_rewards(
        hf hf_version,
        uint64_t distribution_amount,
        const service_nodes::service_node_info& sn_info,
        block_payments& payments) const {
    ZoneScoped;
    // Find out how much is due for the operator: fee_portions/PORTIONS * reward
    assert(sn_info.portions_for_operator <= old::STAKING_PORTIONS);
    uint64_t operator_fee =
            mul128_div64(sn_info.portions_for_operator, distribution_amount, old::STAKING_PORTIONS);

    assert(operator_fee <= distribution_amount);

    // NOTE: Localdev does not have a cryptonote->ETH address step, so, old pre-ETH SN nodes don't
    // have an address assigned to it. This breaks tests that expect pre-ETH SN's to receive
    // funds in order to proceed.
    bool use_eth_address = hf_version >= hf::hf21_eth;
    if (use_eth_address && m_nettype == network_type::LOCALDEV) {
        if (!sn_info.operator_ethereum_address)
            use_eth_address = false;
    }

    // Pay the operator fee to the operator
    if (operator_fee > 0) {
        if (use_eth_address) {
            assert(sn_info.contributors.size());  // NOTE: Be paranoid, check contributors size
            eth::address fee_recipient = sn_info.contributors.size()
                                               ? sn_info.contributors[0].ethereum_beneficiary
                                               : sn_info.operator_ethereum_address;
            payments[fee_recipient] += operator_fee;
        } else {
            payments[sn_info.operator_address] += operator_fee;
        }
    }

    // Pay the balance to all the contributors (including the operator again)
    uint64_t total_contributed_to_sn = std::accumulate(
            sn_info.contributors.begin(),
            sn_info.contributors.end(),
            uint64_t(0),
            [](auto&& a, auto&& b) { return a + b.amount; });

    for (auto& contributor : sn_info.contributors) {
        // This calculates (contributor.amount / total_contributed_to_winner_sn) *
        // (distribution_amount - operator_fee) but using 128 bit integer math
        uint64_t c_reward = mul128_div64(
                contributor.amount, distribution_amount - operator_fee, total_contributed_to_sn);
        if (c_reward > 0) {
            // NOTE: At minimum, when we parsed the contributor if no benficiary is set, it should
            // be assigned to the ethereum address by default.
            auto& balance = use_eth_address ? payments[contributor.ethereum_beneficiary]
                                            : payments[contributor.address];
            balance += c_reward;
        }
    }
}

void BlockchainSQLite::reward_handler(
        const cryptonote::block& block,
        const service_nodes::service_node_list::state_t& service_nodes_state,
        block_payments payments) {
    ZoneScoped;
    assert(block.major_version >= hf::hf19_reward_batching);

    // From here on we calculate everything in milli-atomic OXEN/SENT (i.e. thousanths of an atomic
    // unit) so that our integer math has reduced loss from integer division.
    if (block.reward > std::numeric_limits<uint64_t>::max() / BATCH_REWARD_FACTOR)
        throw oxen::traced<std::logic_error>{"Reward distribution amount is too large"};

    uint64_t block_reward = block.reward * BATCH_REWARD_FACTOR;

    std::lock_guard a_s_lock{address_str_cache_mutex};

    if (block.major_version < feature::ETH_BLS) {
        // Step 1: Pay out the block producer their tx fees (note that, unlike the
        // below, this applies even if the SN isn't currently payable).
        constexpr uint64_t base_sn_reward = oxen::SN_REWARD_HF15 * BATCH_REWARD_FACTOR;
        if (block_reward < base_sn_reward)
            throw oxen::traced<std::logic_error>{"Invalid payment: block reward is too small"};
        if (uint64_t tx_fees = block_reward - base_sn_reward; tx_fees > 0 && block.has_pulse()) {
            auto pulse_leader = service_nodes_state.get_block_producer();
            if (!pulse_leader && !service_nodes_state.sn_list)
                // No sn_list means we're in the test suite, so to make this work, we'll use the
                // block_leader.  (NOTE: this will break if some new core_tests tries expects to
                // award batched backup pulse quorum tx fees as they'll go to the first round
                // leader, rather than the actual producer, but it isn't worth adding to every
                // single state_t to avoid that).
                pulse_leader = service_nodes_state.block_leader;

            if (pulse_leader)
                add_rewards(
                        block.major_version,
                        tx_fees,
                        *service_nodes_state.service_nodes_infos.at(pulse_leader),
                        payments);
        }
        block_reward = base_sn_reward;

        // Step 2: Add Governance reward to the list
        if (m_nettype != cryptonote::network_type::FAKECHAIN) {
            if (parsed_governance_addr.first != block.major_version) {
                cryptonote::get_account_address_from_str(
                        parsed_governance_addr.second,
                        m_nettype,
                        cryptonote::get_config(m_nettype).governance_wallet_address(
                                block.major_version));
                parsed_governance_addr.first = block.major_version;
            }

            uint64_t foundation_reward =
                    cryptonote::governance_reward_formula(block.major_version) *
                    BATCH_REWARD_FACTOR;
            payments[parsed_governance_addr.second.address] += foundation_reward;
        }
    }

    // Step 3: Iterate over the payable (active for >=24h) N service nodes and pay each node 1/N
    // fraction of the total block reward.
    const auto payable_service_nodes =
            service_nodes_state.payable_service_nodes_infos(block.get_height(), m_nettype);
    const uint64_t N = payable_service_nodes.size();
    for (const auto& [node_pubkey, node_info] : payable_service_nodes)
        add_rewards(block.major_version, block_reward / N, *node_info, payments);

    add_sn_rewards(payments);
}

block_payments BlockchainSQLite::get_delayed_payments(uint64_t height) {
    ZoneScoped;
    block_payments result;
    for (auto it : delayed_payments_staging) {
        const std::vector<delayed_payment>& payments = it.second;
        for (auto it : payments) {
            if (it.payout_height == height)
                result[it.exit.addr] += it.exit.amount.to_db();
        }
    }

    return result;
}

bool BlockchainSQLite::add_block(
        const cryptonote::block& block,
        const service_nodes::service_node_list::state_t& service_nodes_state) {
    ZoneScoped;
    auto block_height = block.get_height();
    log::trace(logcat, "BlockchainDB_SQLITE::{} called on height: {}", __func__, block_height);

    auto hf_version = block.major_version;
    if (hf_version < hf::hf19_reward_batching) {
        update_height(block_height, false /*commit*/);
        return true;
    }

    if (block_height ==
        cryptonote::hard_fork_begins(m_nettype, hf::hf19_reward_batching).value_or(0)) {
        log::debug(logcat, "Batching of Service Node Rewards Begins");
        reset_database();
        update_height(block_height - 1, true /*commit*/);
    }

    if (block_height != height + 1) {
        log::error(
                logcat,
                "Block height ({}) out of sync with batching database ({})",
                block_height,
                (height + 1));
        return false;
    }

    // We query our own database as a source of truth to verify the blocks payments against. The
    // calculated_rewards variable contains a known good list of who should have been paid in this
    // block this only applies before the ETH BLS hard fork. After that the rewards are claimed by
    // the users when they wish
    std::vector<cryptonote::batch_sn_payment> calculated_rewards;
    if (hf_version < cryptonote::feature::ETH_BLS) {
        calculated_rewards = get_pre_eth_bls_sn_payments(block_height);
    }

    // We iterate through the block's coinbase payments and build a copy of our own list of the
    // payments miner_tx_vouts this will be compared against calculated_rewards and if they match we
    // know the block is paying the correct people only.
    std::vector<std::pair<crypto::public_key, uint64_t>> miner_tx_vouts;
    if (block.miner_tx)
        for (auto& vout : block.miner_tx->vout)
            miner_tx_vouts.emplace_back(var::get<txout_to_key>(vout.target).key, vout.amount);

    // Goes through the miner transactions vouts checks they are right and marks them as paid in
    // the database
    if (!validate_batch_payment(miner_tx_vouts, calculated_rewards, block_height))
        return false;

    // NOTE: Submit payments
    reward_handler(block, service_nodes_state, get_delayed_payments(block_height));
    update_height(height + 1, false /*commit*/);

    return true;
}

void BlockchainSQLite::add_delayed_payments(
        std::span<const exit_stake> payments, uint64_t at_height, uint64_t delay_blocks) {
    ZoneScoped;
    log::trace(logcat, "BlockchainSQLite::{} called", __func__);

    const int64_t payout_height = at_height + (delay_blocks > 0 ? delay_blocks : 1);
    std::vector<delayed_payment>& dest = delayed_payments_staging[at_height];
    dest.reserve(payments.size());
    for (const auto& it : payments)
        dest.emplace_back(it, payout_height);
}

bool BlockchainSQLite::validate_batch_payment(
        const std::vector<std::pair<crypto::public_key, uint64_t>>& miner_tx_vouts,
        const std::vector<cryptonote::batch_sn_payment>& calculated_payments_from_batching_db,
        uint64_t block_height) {
    ZoneScoped;
    log::trace(logcat, "BlockchainDB_SQLITE::{}", __func__);

    if (miner_tx_vouts.size() != calculated_payments_from_batching_db.size()) {
        log::error(
                logcat,
                "Length of batch payments ({}) does not match block vouts ({})",
                calculated_payments_from_batching_db.size(),
                miner_tx_vouts.size());
        return false;
    }

    uint64_t total_oxen_payout_in_our_db = std::accumulate(
            calculated_payments_from_batching_db.begin(),
            calculated_payments_from_batching_db.end(),
            uint64_t(0),
            [](auto&& a, auto&& b) { return a + b.coin_amount(); });
    uint64_t total_oxen_payout_in_vouts = 0;
    std::vector<batch_sn_payment> finalised_payments;
    cryptonote::keypair const deterministic_keypair =
            cryptonote::get_deterministic_keypair_from_height(block_height);
    for (size_t vout_index = 0; vout_index < miner_tx_vouts.size(); vout_index++) {
        const auto& [pubkey, amt] = miner_tx_vouts[vout_index];
        auto amount = reward_money::coin_amount(amt);
        const auto& from_db = calculated_payments_from_batching_db[vout_index];
        if (amount.to_db() != from_db.amount.to_db()) {
            log::error(
                    logcat,
                    "Batched payout amount incorrect. Should be {}, not {}",
                    from_db.amount.to_db(),
                    amount.to_db());
            return false;
        }
        crypto::public_key out_eph_public_key{};
        if (!cryptonote::get_deterministic_output_key(
                    from_db.address_info.address,
                    deterministic_keypair,
                    vout_index,
                    out_eph_public_key)) {
            log::error(logcat, "Failed to generate output one-time public key");
            return false;
        }
        if (tools::view_guts(pubkey) != tools::view_guts(out_eph_public_key)) {
            log::error(logcat, "Output ephemeral public key does not match");
            return false;
        }
        total_oxen_payout_in_vouts += amount.to_coin();
        finalised_payments.emplace_back(from_db.address_info, amount);
    }
    if (total_oxen_payout_in_vouts != total_oxen_payout_in_our_db) {
        log::error(
                logcat,
                "Total batched payout amount incorrect. Should be {}, not {}",
                total_oxen_payout_in_our_db,
                total_oxen_payout_in_vouts);
        return false;
    }

    return save_payments(block_height, finalised_payments);
}

bool BlockchainSQLite::save_payments(
        uint64_t block_height, const std::vector<batch_sn_payment>& paid_amounts) {
    ZoneScoped;
    log::trace(logcat, "BlockchainDB_SQLITE::{}", __func__);

    block_payments& payments = batched_payments_accrued_staging[block_height];
    for (const batch_sn_payment& it : paid_amounts) {
        auto payment_it = payments.find(it.address_info.address);
        assert(payment_it != payments.end());
        assert(payment_it->second >= it.amount.to_db());
        payments[it.address_info.address] -= it.amount.to_db();
    }

#if 0
    auto select_sum = prepared_st("SELECT amount FROM batched_payments_accrued WHERE address = ?");
    auto update_paid = prepared_st(
            "UPDATE batched_payments_accrued SET amount = (amount - ?) WHERE address = ?");

    std::lock_guard lock{address_str_cache_mutex};
    for (const auto& payment : paid_amounts) {
        const auto address_str = get_address_str(payment);

        if (auto maybe_amount = db::exec_and_maybe_get<int64_t>(select_sum, address_str)) {
            // Truncate the thousanths amount to an atomic OXEN:
            auto amount = static_cast<uint64_t>(*maybe_amount) / BATCH_REWARD_FACTOR *
                          BATCH_REWARD_FACTOR;

            if (amount != payment.amount.to_db()) {
                log::error(
                        logcat,
                        "Invalid amounts passed in to save payments for address {}: received {}, "
                        "expected {} (truncated from {})",
                        address_str,
                        payment.amount.to_db(),
                        amount,
                        *maybe_amount);
                return false;
            }

            db::exec_query(update_paid, static_cast<int64_t>(payment.amount.to_db()), address_str);
            update_paid->reset();
        } else {
            // This shouldn't occur: we validate payout addresses much earlier in the block
            // validation.
            log::error(
                    logcat,
                    "Internal error: Invalid amounts passed in to save payments for address {}: "
                    "that address has no accrued rewards",
                    address_str);
            return false;
        }
        select_sum->reset();
    }
#endif
    return true;
}
}  // namespace cryptonote
