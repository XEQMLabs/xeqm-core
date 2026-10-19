// Copyright (c) 2014-2019, The Monero Project
// Copyright (c)      2019, The Loki Project
//

// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Parts of this file are originally copyright (c) 2012-2013 The Cryptonote developers

// PoW block production has been retired. XEQM is a Pulse chain, and the signed fallback
// (feature::pulse_recovery) replaced the RandomX PoW fallback at mainnet height 236920, so no
// freshly PoW-mined block is acceptable any more. The RandomX mining worker machinery that used
// to live here (worker threads, hash-rate tracking, rx_slow_hash_allocate_state/free,
// rx_stop_mining, and the --start-mining/--mining-threads options) has been removed. What remains
// is find_nonce_for_given_block (used only for genesis generation) and a set of inert lifecycle
// hooks still called from cryptonote_core. PoW VERIFICATION of historical blocks is unaffected;
// it lives in blockchain.cpp, cryptonote_tx_utils.cpp and src/crypto/rx-slow-hash.c.

#include <limits>
#include <stdexcept>

#include "common/command_line.h"
#include "common/exception.h"
#include "common/util.h"
#include "cryptonote_basic/cryptonote_format_utils.h"

#include "miner.h"

namespace cryptonote {

miner::miner(
        get_block_hash_cb hash,
        handle_block_found_cb found,
        create_next_miner_block_template_cb create) :
        m_get_block_hash{std::move(hash)},
        m_handle_block_found{std::move(found)},
        m_create_next_miner_block_template{std::move(create)} {
    if (!(m_get_block_hash && m_handle_block_found && m_create_next_miner_block_template))
        throw oxen::traced<std::invalid_argument>{
                "Invalid miner constructor: required callbacks cannot be null"};
}
//-----------------------------------------------------------------------------------------------------
miner::~miner() = default;
//-----------------------------------------------------------------------------------------------------
// PoW mining has been retired; there are no mining-specific daemon options left.
void miner::init_options(boost::program_options::options_description& /*desc*/) {}
//-----------------------------------------------------------------------------------------------------
bool miner::init(const boost::program_options::variables_map& /*vm*/, network_type /*nettype*/) {
    return true;
}
//-----------------------------------------------------------------------------------------------------
// Inert lifecycle hooks. PoW production is gone, so these do nothing; they exist only so the
// cryptonote_core call sites (on_idle/on_block_chain_update/pause/resume/on_synchronized/stop)
// remain valid.
bool miner::on_block_chain_update() {
    return true;
}
//-----------------------------------------------------------------------------------------------------
bool miner::on_idle() {
    return true;
}
//-----------------------------------------------------------------------------------------------------
void miner::on_synchronized() {}
//-----------------------------------------------------------------------------------------------------
void miner::pause() {}
//-----------------------------------------------------------------------------------------------------
void miner::resume() {}
//-----------------------------------------------------------------------------------------------------
bool miner::stop() {
    return true;
}
//-----------------------------------------------------------------------------------------------------
// Genesis nonce generation only (cryptonote_tx_utils.cpp, tools/generate_genesis.cpp). The hashing
// is performed by the supplied callback; this is not part of any running-node mining path.
bool miner::find_nonce_for_given_block(
        const get_block_hash_cb& gbh, block& bl, const difficulty_type& diffic, uint64_t height) {
    for (; bl.nonce != std::numeric_limits<uint32_t>::max(); bl.nonce++) {
        crypto::hash h;
        gbh(bl, height, tools::get_max_concurrency(), h);

        if (check_hash(h, diffic)) {
            bl.invalidate_hashes();
            return true;
        }
    }
    bl.invalidate_hashes();
    return false;
}
}  // namespace cryptonote
