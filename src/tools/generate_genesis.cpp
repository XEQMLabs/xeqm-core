#include "../cryptonote_core/cryptonote_core.h"
#include "../cryptonote_basic/cryptonote_format_utils.h"
#include "../cryptonote_basic/cryptonote_basic.h"
#include "../common/string_util.h"
#include "../serialization/binary_utils.h"
#include <oxenc/hex.h>
#include <iostream>
#include <string>

using namespace cryptonote;

// Create a genesis transaction with no premine
std::string create_genesis_tx() {
    transaction tx;
    tx.version = txversion::v1;
    tx.unlock_time = MINED_MONEY_UNLOCK_WINDOW;
    
    // Coinbase input
    txin_gen in;
    in.height = 0;
    tx.vin.push_back(in);
    
    // Required tx extra fields
    keypair txkey{hw::get_device("default")};
    add_tx_extra<tx_extra_pub_key>(tx, txkey.pub);
    
    // Create a new wallet address with the correct prefix
    account_base genesis_account;
    genesis_account.generate();
    
    // Add an output to the genesis account
    txout_to_key out;
    out.key = genesis_account.get_keys().m_account_address.m_spend_public_key;
    
    tx_out output;
    output.amount = 22500000000000000; // 22.5 million atomic units (within max block reward)
    output.target = out;
    
    tx.vout.push_back(output);
    tx.output_unlock_times.push_back(0);
    
    // Print the genesis wallet address and keys
    std::cout << "Genesis wallet address: " 
              << get_account_address_as_str(network_type::MAINNET, 
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

int main() {
    try {
        hw::get_device("default"); // Initialize crypto via hardware device
        
        // Generate genesis transaction
        std::string genesis_tx = create_genesis_tx();
        
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
