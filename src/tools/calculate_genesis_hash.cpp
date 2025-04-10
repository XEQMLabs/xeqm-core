#include "../cryptonote_core/cryptonote_core.h"
#include "../cryptonote_basic/cryptonote_format_utils.h"
#include "../cryptonote_basic/cryptonote_basic.h"
#include "../common/string_util.h"
#include "../serialization/binary_utils.h"
#include "../network_config/mainnet.h"
#include <oxenc/hex.h>
#include <iostream>
#include <string>

using namespace cryptonote;

int main() {
    try {
        hw::get_device("default"); // Initialize crypto via hardware device
        
        // Get genesis transaction from network configuration
        std::string genesis_tx = std::string(config::mainnet::config.GENESIS_TX);
        
        // Create genesis block
        block genesis;
        genesis.major_version = hf::hf7;
        genesis.minor_version = static_cast<uint8_t>(hf::hf7);
        genesis.timestamp = 1710140000;  // March 11, 2024 @ 8:00am UTC
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
        
        // Calculate block hash
        crypto::hash block_hash = get_block_hash(genesis);
        
        // Output results
        std::cout << "Genesis TX: " << genesis_tx << std::endl;
        std::cout << "Genesis Block Hash: " << oxenc::to_hex(std::string_view{reinterpret_cast<const char*>(block_hash.data()), sizeof(block_hash)}) << std::endl;
        
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
} 