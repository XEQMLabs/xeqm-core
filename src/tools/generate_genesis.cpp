#include <oxenc/hex.h>

#include <iostream>
#include <string>

#include "../common/string_util.h"
#include "../cryptonote_basic/cryptonote_basic.h"
#include "../cryptonote_basic/cryptonote_format_utils.h"
#include "../cryptonote_core/cryptonote_core.h"
#include "../cryptonote_core/cryptonote_tx_utils.h"
#include "../serialization/binary_utils.h"

using namespace cryptonote;

// Create a genesis transaction with no premine
std::string create_genesis_tx() {
    transaction tx;
    tx.version = txversion::v1;
    tx.unlock_time = 0;  // No unlock time

    // Create a new wallet address with the correct prefix
    account_base genesis_account;
    genesis_account.generate();

    // Create miner tx context
    oxen_miner_tx_context miner_tx_context = oxen_miner_tx_context::miner_block(
            network_type::MAINNET,
            genesis_account.get_keys().m_account_address);

    // Construct miner tx
    std::vector<tx_destination_entry> destinations;
    tx_destination_entry de;
    de.amount = 22500000000000000;  // 22.5 million atomic units
    de.addr = genesis_account.get_keys().m_account_address;
    destinations.push_back(de);

    std::vector<uint8_t> extra;
    keypair txkey{hw::get_device("default")};
    add_tx_extra<tx_extra_pub_key>(extra, txkey.pub);

    // Construct the transaction
    std::pair<bool, uint64_t> result = construct_miner_tx(
            0,  // height
            0,  // median_weight
            0,  // already_generated_coins
            0,  // current_block_weight
            0,  // fee
            tx,
            miner_tx_context,
            {},  // sn_rewards
            "",  // extra_nonce
            hf::hf7);  // hard_fork_version

    if (!result.first) {
        throw std::runtime_error("Failed to construct miner tx");
    }

    // Print the genesis wallet address and keys
    std::cout << "Genesis wallet address: "
              << get_account_address_as_str(
                         network_type::MAINNET, false, genesis_account.get_keys().m_account_address)
              << std::endl;

    std::cout << "Genesis spend private key: "
              << tools::hex_guts(genesis_account.get_keys().m_spend_secret_key) << std::endl;

    std::cout << "Genesis view private key: "
              << tools::hex_guts(genesis_account.get_keys().m_view_secret_key) << std::endl;

    std::string tx_blob;
    if (!t_serializable_object_to_blob(tx, tx_blob)) {
        throw std::runtime_error("Failed to serialize genesis transaction");
    }

    return oxenc::to_hex(tx_blob);
}

int main() {
    try {
        hw::get_device("default");  // Initialize crypto via hardware device

        // Generate genesis transaction
        std::string genesis_tx = create_genesis_tx();

        // Create genesis block
        block genesis;
        genesis.major_version = hf::hf7;
        genesis.minor_version = static_cast<uint8_t>(hf::hf7);
        genesis.timestamp = 0;  // Must be 0
        genesis.prev_id = crypto::hash{};
        genesis.nonce = 12345;  // Must match GENESIS_NONCE

        // Parse genesis transaction
        std::string tx_blob;
        if (!oxenc::is_hex(genesis_tx))
            throw std::runtime_error("Failed to parse genesis tx hex");

        tx_blob = oxenc::from_hex(genesis_tx);

        transaction parsed_tx;
        crypto::hash tx_hash;
        if (!parse_and_validate_tx_from_blob(tx_blob, parsed_tx, tx_hash))
            throw std::runtime_error("Failed to validate genesis tx");

        genesis.miner_tx = parsed_tx;

        // Calculate block hash using the same method as the daemon
        crypto::hash block_hash = get_block_longhash(
            network_type::UNDEFINED,
            randomx_longhash_context(NULL, genesis, 0),
            genesis,
            0,
            0);

        // Output results
        std::cout << "Genesis TX: " << genesis_tx << std::endl;
        std::cout << "Genesis Block Hash: "
                  << oxenc::to_hex(std::string_view{
                             reinterpret_cast<const char*>(block_hash.data()), sizeof(block_hash)})
                  << std::endl;

        // Print block details for debugging
        std::cout << "\nBlock details:\n";
        std::cout << "Major version: " << (int)genesis.major_version << "\n";
        std::cout << "Minor version: " << (int)genesis.minor_version << "\n";
        std::cout << "Timestamp: " << genesis.timestamp << "\n";
        std::cout << "Nonce: " << genesis.nonce << "\n";
        std::cout << "Prev ID: " << oxenc::to_hex(std::string_view{
                             reinterpret_cast<const char*>(genesis.prev_id.data()), sizeof(genesis.prev_id)}) << "\n";
        std::cout << "Miner tx hash: " << oxenc::to_hex(std::string_view{
                             reinterpret_cast<const char*>(tx_hash.data()), sizeof(tx_hash)}) << "\n";

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
