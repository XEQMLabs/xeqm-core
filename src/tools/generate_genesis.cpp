#include <cryptonote_core/cryptonote_core.h>
#include <cryptonote_basic/cryptonote_format_utils.h>
#include <cryptonote_basic/cryptonote_basic.h>
#include <iostream>
#include <string>

using namespace cryptonote;

// Create a genesis transaction with no premine
std::string create_genesis_tx() {
    transaction tx;
    tx.version = 1;
    tx.unlock_time = 0;
    
    // No inputs (coinbase)
    tx.vin.clear();
    
    // No outputs (no premine)
    tx.vout.clear();
    
    // Empty extra field
    tx.extra.clear();
    
    // Serialize transaction to hex
    std::string tx_blob;
    bool success = get_transaction_blob(tx, tx_blob);
    if (!success) {
        throw std::runtime_error("Failed to serialize genesis transaction");
    }
    
    return string_tools::buff_to_hex_nodelimer(tx_blob);
}

int main() {
    try {
        // Generate genesis transaction
        std::string genesis_tx = create_genesis_tx();
        
        // Create genesis block
        block genesis;
        genesis.major_version = 1;
        genesis.minor_version = 0;
        genesis.timestamp = GENESIS_TIMESTAMP;  // From cryptonote_config.h
        genesis.prev_id = crypto::null_hash;
        
        // Parse genesis transaction
        std::string tx_blob;
        string_tools::parse_hexstr_to_binbuff(genesis_tx, tx_blob);
        parse_and_validate_tx_from_blob(tx_blob, genesis.miner_tx);
        
        // Set nonce
        genesis.nonce = GENESIS_BLOCK_NONCE;
        
        // Calculate genesis block hash
        crypto::hash genesis_hash = get_block_hash(genesis);
        
        // Print results
        std::cout << "Genesis Transaction: " << genesis_tx << std::endl;
        std::cout << "Genesis Block Hash: " << genesis_hash << std::endl;
        std::cout << "\nConfig Format:" << std::endl;
        std::cout << ".GENESIS_TX = \"" << genesis_tx << "\"sv," << std::endl;
        std::cout << ".GENESIS_NONCE = " << GENESIS_BLOCK_NONCE << "," << std::endl;
        std::cout << ".HEIGHT_ESTIMATE_TIMESTAMP = " << GENESIS_TIMESTAMP << "," << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;we 
        return 1;
    }
    
    return 0;
}
