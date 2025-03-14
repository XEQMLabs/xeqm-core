#include "../cryptonote_basic/cryptonote_basic.h"
#include "../cryptonote_basic/cryptonote_basic_impl.h"
#include "../common/base58.h"
#include "../serialization/binary_utils.h"
#include <iostream>
#include <iomanip>

using namespace cryptonote;
using namespace tools;
using namespace serialization;

void test_prefix_range(uint64_t start, uint64_t end) {
    // Create a dummy address for testing
    account_public_address dummy_address{};
    
    for (uint64_t prefix = start; prefix <= end; prefix++) {
        // Get address string with current prefix
        std::string addr = base58::encode_addr(prefix, dump_binary(dummy_address));
        
        // Check if it starts with "Xeq"
        if (addr.substr(0, 3) == "Xeq") {
            std::cout << "Found matching prefix: " << prefix << " (0x" 
                     << std::hex << std::setfill('0') << std::setw(6) << prefix << ")\n";
            std::cout << "Sample address: " << addr << "\n";
            
            // Verify decoding works
            address_parse_info info;
            std::string data;
            uint64_t decoded_prefix;
            if (base58::decode_addr(addr, decoded_prefix, data)) {
                std::cout << "Verified: prefix decodes correctly back to " << std::dec 
                         << decoded_prefix << " (0x" << std::hex << decoded_prefix << ")\n";
                return;
            }
        }
        
        // Progress indicator every million attempts
        if (prefix % 1000000 == 0) {
            std::cout << "Tested up to: " << std::dec << prefix << " (0x" 
                     << std::hex << prefix << ")\n";
        }
    }
}

int main() {
    std::cout << "Searching for address prefix that generates 'Xeq' addresses...\n";
    
    // Test different ranges of prefixes
    // Start with smaller numbers first as they're more likely to produce shorter addresses
    test_prefix_range(1, 1000000);           // First million
    test_prefix_range(1000000, 10000000);    // Next 9 million
    test_prefix_range(10000000, 100000000);  // Next 90 million if needed
    
    std::cout << "No matching prefix found in tested range\n";
    return 1;
}
