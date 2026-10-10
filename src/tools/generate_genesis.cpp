#include <oxenc/hex.h>

#include <iostream>
#include <string>
#include <boost/program_options.hpp>

#include "../common/command_line.h"
#include "../common/string_util.h"
#include "../common/guts.h"
#include "../cryptonote_basic/cryptonote_basic.h"
#include "../cryptonote_basic/cryptonote_format_utils.h"
#include "../cryptonote_basic/miner.h"
#include "../cryptonote_core/cryptonote_core.h"
#include "../cryptonote_core/cryptonote_tx_utils.h"
#include "../serialization/binary_utils.h"

namespace po = boost::program_options;
using namespace cryptonote;

// Create a genesis transaction for the specified network
std::string create_genesis_tx(network_type nettype, account_base& genesis_account) {
    transaction tx;
    tx.version = txversion::v1;
    tx.unlock_time = 0;  // No unlock time

    // Create the genesis account.
    if (nettype == network_type::TESTNET) {
        // Premine-treasury (testnet signer-bootstrap): instead of a throwaway random account whose
        // printed keys are then discarded (which leaves the genesis premine unspendable), premine to
        // the harness's FIXED treasury wallet so full-setup.py -- which already restores this wallet
        // at height 0 -- can fund service-node operators directly from genesis with no mining. These
        // keys are testnet-only (xeqm-testnet full-setup.py GOV_* constants) and the coins are
        // worthless outside the private testnet.
        constexpr std::string_view TREASURY_ADDRESS =
                "XEQTCMd5W5jX582CXuuQwzc6KY5mEHvXRNnLxoNQ6nA7Wq1ioBudM64Z835ygvWcrxTHyuuN5suvPiapbjbVAFeZ3NXFhKG2fx";
        constexpr std::string_view TREASURY_SPEND_KEY =
                "0c999a88be252215b48735272e5cf3dd86d54f850bb0bc8bfe9aea0de7549405";
        constexpr std::string_view TREASURY_VIEW_KEY =
                "8e34dd7f6eb9b9f28be81619e20765c53b79070aab49c353ffc877b67389cd09";
        cryptonote::address_parse_info info{};
        if (!cryptonote::get_account_address_from_str(info, nettype, std::string(TREASURY_ADDRESS)))
            throw std::runtime_error("Failed to parse testnet treasury address");
        auto spend = tools::make_from_hex_guts<crypto::secret_key>(TREASURY_SPEND_KEY);
        auto view = tools::make_from_hex_guts<crypto::secret_key>(TREASURY_VIEW_KEY);
        genesis_account.create_from_keys(info.address, spend, view);
        // Fail loudly if the keys do not derive the expected treasury address -- that would silently
        // recreate the unspendable-premine bug this change exists to fix.
        if (genesis_account.get_keys().m_account_address != info.address)
            throw std::runtime_error("Treasury keys do not derive the expected treasury address");
    } else {
        genesis_account.generate();
    }

    // Create miner tx context for the correct network
    oxen_miner_tx_context miner_tx_context = oxen_miner_tx_context::miner_block(
            nettype,
            genesis_account.get_keys().m_account_address);

    // Construct miner tx
    std::vector<tx_destination_entry> destinations;
    tx_destination_entry de;
    // 276,786,541.3247 XEQ × 10^9 = 276,786,541,324,700,000 atomic units
    de.amount = 276786541324700000;
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
                         nettype, false, genesis_account.get_keys().m_account_address)
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

int main(int argc, char* argv[]) {
    std::string network;

    po::options_description desc("Genesis block generator");
    desc.add_options()
        ("help,h", "produce help message")
        ("network,n", po::value<std::string>(&network)->default_value("mainnet"),
         "Network type: mainnet, testnet, devnet");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << desc << std::endl;
        return 1;
    }

    // Parse network type
    network_type nettype;
    if (network == "testnet") {
        nettype = network_type::TESTNET;
    } else if (network == "devnet") {
        nettype = network_type::DEVNET;
    } else if (network == "mainnet") {
        nettype = network_type::MAINNET;
    } else {
        std::cerr << "Error: Unknown network type '" << network << "'. Use: mainnet, testnet, devnet" << std::endl;
        return 1;
    }

    std::cout << "Network: " << network << std::endl;

    try {
        hw::get_device("default");  // Initialize crypto via hardware device

        // Generate genesis transaction
        account_base genesis_account;
        std::string genesis_tx = create_genesis_tx(nettype, genesis_account);

        // Create genesis block
        block genesis;
        genesis.major_version = hf::hf7;
        genesis.minor_version = static_cast<uint8_t>(hf::hf7);
        genesis.timestamp = 0;  // Must be 0
        genesis.prev_id = crypto::hash{};

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

        // Get config for GENESIS_NONCE
        auto& conf = get_config(nettype);
        genesis.nonce = conf.GENESIS_NONCE;

        // Mine the genesis block using the same method as the daemon
        // This finds a valid nonce starting from GENESIS_NONCE
        miner::find_nonce_for_given_block(
                [](const cryptonote::block& b,
                   uint64_t height,
                   unsigned int threads,
                   crypto::hash& hash) {
                    // Daemon uses UNDEFINED for genesis block longhash
                    hash = cryptonote::get_block_longhash(
                            network_type::UNDEFINED,
                            cryptonote::randomx_longhash_context(NULL, b, height),
                            b,
                            height,
                            threads);
                    return true;
                },
                genesis,
                1,   // difficulty = 1
                0);  // height = 0

        genesis.invalidate_hashes();

        // Calculate final block hash
        crypto::hash block_hash = get_block_hash(genesis);

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
            reinterpret_cast<const char*>(genesis.prev_id.data(), sizeof(genesis.prev_id))}) << "\n";
        std::cout << "Miner tx hash: " << oxenc::to_hex(std::string_view{
            reinterpret_cast<const char*>(tx_hash.data(), sizeof(tx_hash))}) << "\n";

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
