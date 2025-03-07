#include "detail.h"

namespace oxen::sent {

using addrmap_t = std::unordered_map<std::string, eth::address>;
using conv_ratio_t = std::pair<std::uint8_t, std::uint8_t>;
using bonus_map_t = std::unordered_map<eth::address, std::uint64_t>;
using proper_ed_keys_t = std::unordered_map<crypto::public_key, crypto::ed25519_public_key>;
using bls_keys_t = std::unordered_map<crypto::ed25519_public_key, eth::bls_public_key>;

namespace mainnet {
    const addrmap_t addresses;
    const proper_ed_keys_t proper_ed_keys;
    const bls_keys_t bls_keys;
    const conv_ratio_t conv_ratio;
    const bonus_map_t transition_bonus;
}  // namespace mainnet

const conv_ratio_t& conversion_ratio(network_type net) {
    return net == network_type::DEVNET   ? devnet::conv_ratio
         : net == network_type::TESTNET  ? testnet::conv_ratio
         : net == network_type::LOCALDEV ? localdev::conv_ratio
                                         : mainnet::conv_ratio;
}

const addrmap_t& addresses(network_type net) {
    return net == network_type::DEVNET   ? devnet::addresses
         : net == network_type::TESTNET  ? testnet::addresses
         : net == network_type::LOCALDEV ? localdev::addresses
                                         : mainnet::addresses;
}

const bonus_map_t& transition_bonus(network_type net) {
    return net == network_type::DEVNET   ? devnet::transition_bonus
         : net == network_type::TESTNET  ? testnet::transition_bonus
         : net == network_type::LOCALDEV ? localdev::transition_bonus
                                         : mainnet::transition_bonus;
}

const proper_ed_keys_t proper_ed_keys(network_type net) {
    return net == network_type::DEVNET   ? devnet::proper_ed_keys
         : net == network_type::TESTNET  ? testnet::proper_ed_keys
         : net == network_type::LOCALDEV ? localdev::proper_ed_keys
                                         : mainnet::proper_ed_keys;
}

const bls_keys_t bls_keys(network_type net) {
    return net == network_type::DEVNET   ? devnet::bls_keys
         : net == network_type::TESTNET  ? testnet::bls_keys
         : net == network_type::LOCALDEV ? localdev::bls_keys
                                         : mainnet::bls_keys;
}

}  // namespace oxen::sent
