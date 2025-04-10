#include <iostream>
#include <string>
#include <ctime>
#include <boost/program_options.hpp>
#include "cryptonote_core/cryptonote_core.h"
#include "cryptonote_basic/cryptonote_basic.h"
#include "cryptonote_basic/cryptonote_format_utils.h"
#include "common/command_line.h"
#include "crypto/crypto.h"
#include "checkpoints/checkpoints.h"
#include "device/device.hpp"
#include "serialization/binary_utils.h"
#include "common/file.h"
#include <oxenc/hex.h>

namespace po = boost::program_options;

// Create a genesis transaction with no premine
std::string create_genesis_tx() {
    cryptonote::transaction tx;
    tx.version = cryptonote::txversion::v1;
    tx.unlock_time = cryptonote::MINED_MONEY_UNLOCK_WINDOW;
    
    // Coinbase input
    cryptonote::txin_gen in;
    in.height = 0;
    tx.vin.push_back(in);
    
    // Required tx extra fields
    cryptonote::keypair txkey{hw::get_device("default")};
    cryptonote::add_tx_extra<cryptonote::tx_extra_pub_key>(tx, txkey.pub);
    
    // Create a new wallet address with the correct prefix
    cryptonote::account_base genesis_account;
    genesis_account.generate();
    
    // Add an output to the genesis account
    cryptonote::txout_to_key out;
    out.key = genesis_account.get_keys().m_account_address.m_spend_public_key;
    
    cryptonote::tx_out output;
    output.amount = 22500000000000000; // 22.5 million atomic units (within max block reward)
    output.target = out;
    
    tx.vout.push_back(output);
    tx.output_unlock_times.push_back(0);
    
    // Print the genesis wallet address and keys
    std::cout << "Genesis wallet address: " 
              << cryptonote::get_account_address_as_str(cryptonote::network_type::MAINNET, 
                                          false, 
                                          genesis_account.get_keys().m_account_address) 
              << std::endl;
              
    std::cout << "Genesis spend private key: " 
              << tools::hex_guts(genesis_account.get_keys().m_spend_secret_key)
              << std::endl;
              
    std::cout << "Genesis view private key: " 
              << tools::hex_guts(genesis_account.get_keys().m_view_secret_key)
              << std::endl;
    
    std::string tx_blob;
    if (!t_serializable_object_to_blob(tx, tx_blob)) {
        throw std::runtime_error("Failed to serialize genesis transaction");
    }
    
    return oxenc::to_hex(tx_blob);
}

int main(int argc, char* argv[]) {
    std::string genesis_tx;
    uint64_t nonce = 10000; // Default nonce
    bool add_checkpoint = false;
    std::string checkpoint_file;

    po::options_description desc("Allowed options");
    desc.add_options()
        ("help,h", "produce help message")
        ("genesis-tx", po::value<std::string>(&genesis_tx), "Genesis transaction in hex format (if not provided, will generate new)")
        ("nonce", po::value<uint64_t>(&nonce), "Nonce value (default: 10000)")
        ("add-checkpoint", "Add the generated block as a checkpoint")
        ("checkpoint-file", po::value<std::string>(&checkpoint_file), "Checkpoint file to update (if adding checkpoint)");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << desc << std::endl;
        return 1;
    }

    add_checkpoint = vm.count("add-checkpoint");
    if (add_checkpoint && !vm.count("checkpoint-file")) {
        std::cerr << "Error: --checkpoint-file is required when using --add-checkpoint" << std::endl;
        return 1;
    }

    try {
        // Initialize crypto via hardware device
        hw::get_device("default");
        
        // Generate or use provided genesis transaction
        if (!vm.count("genesis-tx")) {
            genesis_tx = create_genesis_tx();
            std::cout << "Generated new genesis transaction" << std::endl;
        }

        // Create genesis block
        cryptonote::block genesis_block;
        genesis_block.major_version = cryptonote::hf::hf7;
        genesis_block.minor_version = static_cast<uint8_t>(cryptonote::hf::hf7);
        genesis_block.timestamp = 1710140000;  // March 11, 2024 @ 8:00am UTC
        genesis_block.prev_id = crypto::hash{};
        genesis_block.nonce = nonce;

        // Parse and validate genesis transaction
        std::string tx_blob;
        if (!oxenc::is_hex(genesis_tx)) {
            std::cerr << "Error: Invalid genesis tx hex format" << std::endl;
            return 1;
        }
            
        tx_blob = oxenc::from_hex(genesis_tx);
            
        cryptonote::transaction parsed_tx;
        crypto::hash tx_hash;
        if (!cryptonote::parse_and_validate_tx_from_blob(tx_blob, parsed_tx, tx_hash)) {
            std::cerr << "Error: Failed to validate genesis tx" << std::endl;
            return 1;
        }
            
        genesis_block.miner_tx = parsed_tx;

        // Calculate block hash
        crypto::hash block_hash = cryptonote::get_block_hash(genesis_block);

        // Output the block hash
        std::cout << "Genesis Block Hash: " << oxenc::to_hex(std::string_view{reinterpret_cast<const char*>(block_hash.data()), sizeof(block_hash)}) << std::endl;

        // Add checkpoint if requested
        if (add_checkpoint) {
            cryptonote::height_to_hash_json checkpoints;
            std::vector<cryptonote::height_to_hash> checkpoint_hashes;
            
            // Load existing checkpoints if file exists
            if (cryptonote::load_checkpoints_from_json(checkpoint_file, checkpoint_hashes)) {
                checkpoints.hashlines = std::move(checkpoint_hashes);
            }

            // Add genesis block checkpoint
            cryptonote::height_to_hash genesis_checkpoint;
            genesis_checkpoint.height = 0;
            genesis_checkpoint.hash = oxenc::to_hex(std::string_view{reinterpret_cast<const char*>(block_hash.data()), sizeof(block_hash)});
            checkpoints.hashlines.push_back(genesis_checkpoint);

            // Save updated checkpoints
            std::string json = epee::serialization::store_t_to_json(checkpoints);
            if (!tools::dump_file(checkpoint_file, json)) {
                std::cerr << "Failed to save checkpoint file" << std::endl;
                return 1;
            }
            std::cout << "Added genesis block checkpoint to " << checkpoint_file << std::endl;
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
} 