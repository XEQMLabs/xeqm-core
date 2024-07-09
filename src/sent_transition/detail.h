#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "crypto/crypto.h"
#include "crypto/eth.h"

namespace oxen::sent::devnet {
extern const std::unordered_map<std::string, eth::address> addresses;
extern const std::unordered_map<crypto::public_key, crypto::ed25519_public_key> proper_ed_keys;
extern const std::unordered_map<crypto::ed25519_public_key, eth::bls_public_key> bls_keys;
extern const std::pair<std::uint8_t, std::uint8_t> conv_ratio;
extern const std::unordered_map<eth::address, std::uint64_t> transition_bonus;
}  // namespace oxen::sent::devnet

namespace oxen::sent::testnet {
extern const std::unordered_map<std::string, eth::address> addresses;
extern const std::unordered_map<crypto::public_key, crypto::ed25519_public_key> proper_ed_keys;
extern const std::unordered_map<crypto::ed25519_public_key, eth::bls_public_key> bls_keys;
extern const std::pair<std::uint8_t, std::uint8_t> conv_ratio;
extern const std::unordered_map<eth::address, std::uint64_t> transition_bonus;
}  // namespace oxen::sent::testnet

namespace oxen::sent::localdev {
extern const std::unordered_map<std::string, eth::address> addresses;
extern const std::unordered_map<crypto::public_key, crypto::ed25519_public_key> proper_ed_keys;
extern const std::unordered_map<crypto::ed25519_public_key, eth::bls_public_key> bls_keys;
extern const std::pair<std::uint8_t, std::uint8_t> conv_ratio;
extern const std::unordered_map<eth::address, std::uint64_t> transition_bonus;
}  // namespace oxen::sent::localdev

namespace oxen::sent::mainnet {
extern const std::unordered_map<std::string, eth::address> addresses;
extern const std::unordered_map<crypto::public_key, crypto::ed25519_public_key> proper_ed_keys;
extern const std::unordered_map<crypto::ed25519_public_key, eth::bls_public_key> bls_keys;
extern const std::pair<std::uint8_t, std::uint8_t> conv_ratio;
extern const std::unordered_map<eth::address, std::uint64_t> transition_bonus;
}  // namespace oxen::sent::mainnet
