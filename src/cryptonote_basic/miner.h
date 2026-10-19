// Copyright (c) 2014-2019, The Monero Project
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

#pragma once

#include <atomic>
#include <thread>

#include "common/fs.h"
#include "common/periodic_task.h"
#include "cryptonote_basic/cryptonote_basic.h"
#include "cryptonote_basic/difficulty.h"
#include "cryptonote_basic/verification_context.h"
#ifdef _WIN32
#include <windows.h>
#endif

namespace boost::program_options {
class variables_map;
class options_description;
}  // namespace boost::program_options

namespace cryptonote {
using namespace std::literals;

struct i_miner_handler {
    virtual bool handle_block_found(block& b, block_verification_context& bvc) = 0;
    virtual bool create_next_miner_block_template(
            block& b,
            const account_public_address& adr,
            difficulty_type& diffic,
            uint64_t& height,
            uint64_t& expected_reward,
            const std::string& ex_nonce) = 0;

  protected:
    ~i_miner_handler() {};
};

/************************************************************************/
/* PoW block production has been retired.                               */
/*                                                                      */
/* XEQM is a Pulse chain; the signed fallback (feature::pulse_recovery) */
/* replaced the RandomX PoW fallback at mainnet height 236920, so no    */
/* freshly PoW-mined block is acceptable any more. The mining worker    */
/* machinery that used to run here has been removed. What remains is:    */
/*   - find_nonce_for_given_block, used only for genesis generation, and */
/*   - inert lifecycle hooks still called from cryptonote_core.          */
/* This class no longer spins up any mining threads or touches RandomX.  */
/* PoW VERIFICATION of historical blocks is unaffected (it lives in      */
/* blockchain.cpp / cryptonote_tx_utils.cpp / rx-slow-hash.c).           */
/************************************************************************/
class miner {
  public:
    using get_block_hash_cb =
            std::function<bool(const cryptonote::block&, uint64_t, unsigned int, crypto::hash&)>;
    using handle_block_found_cb = std::function<bool(block& b, block_verification_context& bvc)>;
    using create_next_miner_block_template_cb = std::function<bool(
            block& b,
            const account_public_address& adr,
            difficulty_type& diffic,
            uint64_t& height,
            uint64_t& expected_reward,
            const std::string& ex_nonce)>;

    miner(get_block_hash_cb hash,
          handle_block_found_cb found,
          create_next_miner_block_template_cb create);
    ~miner();
    bool init(const boost::program_options::variables_map& vm, network_type nettype);
    static void init_options(boost::program_options::options_description& desc);
    // Inert lifecycle hooks (no-ops): kept so cryptonote_core's call sites are undisturbed.
    bool on_block_chain_update();
    bool stop();
    bool on_idle();
    void on_synchronized();
    // synchronous analog (for fast calls) -- used for genesis nonce generation only.
    static bool find_nonce_for_given_block(
            const get_block_hash_cb& gbh,
            block& bl,
            const difficulty_type& diffic,
            uint64_t height);
    void pause();
    void resume();

  private:
    // Retained from the construction contract in cryptonote_core; unused now that PoW
    // production is gone, but kept so the core wiring does not need to change.
    get_block_hash_cb m_get_block_hash;
    handle_block_found_cb m_handle_block_found;
    create_next_miner_block_template_cb m_create_next_miner_block_template;
};
}  // namespace cryptonote
