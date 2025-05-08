#include <oxenc/hex.h>

#include <cstdint>
#include <string>
#include <unordered_map>

#include "common/guts.h"
#include "crypto/crypto.h"
#include "crypto/eth.h"
#include "detail.h"

namespace oxen::sent::mainnet {

using namespace std::literals;

const std::unordered_map<std::string, eth::address> addresses{
};

const std::unordered_map<crypto::public_key, crypto::ed25519_public_key> proper_ed_keys{
};

const std::unordered_map<crypto::ed25519_public_key, eth::bls_public_key> bls_keys{
};

const std::pair<std::uint64_t, std::uint64_t> conv_ratio{2, 3}; // dummy value

const std::unordered_map<eth::address, std::uint64_t> transition_bonus{
};

}  // namespace oxen::sent::mainnet
