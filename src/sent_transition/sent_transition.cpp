#include "sent_transition.h"

#include <ranges>
#include <fmt/os.h>

#include "crypto/crypto.h"
#include "cryptonote_basic/cryptonote_basic.h"
#include "cryptonote_basic/cryptonote_basic_impl.h"
#include "detail.h"
#include "network_config/mocknet.h"
#include "logging/oxen_logger.h"

namespace oxen::sent {

inline auto logcat = oxen::log::Cat("sent_transition");

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

struct transition_context {
    addrmap_t addresses;
    proper_ed_keys_t proper_ed_keys;
    bls_keys_t bls_keys;
    conv_ratio_t conv_ratio;
    bonus_map_t transition_bonus;
    uint64_t staking_requirement;
    std::pair<uint32_t, uint32_t> staking_ratio;
    uint64_t oxen_staking_requirement;
};

static transition_context get_transition_context(network_type net, service_nodes::service_node_list::state_t& snl_state)
{
    transition_context result = {};
    if (net == cryptonote::network_type::MAINNET) {
        result.staking_requirement = SENT_STAKING_REQUIREMENT;
        result.staking_ratio = OXEN_SENT_STAKING_RATIO;
        result.oxen_staking_requirement = OXEN_STAKING_REQUIREMENT;
    } else {
        result.staking_requirement = SENT_STAKING_REQUIREMENT_TESTNET;
        result.staking_ratio = OXEN_SENT_TESTNET_STAKING_RATIO;
        result.oxen_staking_requirement = OXEN_STAKING_REQUIREMENT_TESTNET;
    }

    switch (net) {
        case network_type::TESTNET:
            result.addresses = testnet::addresses;
            result.proper_ed_keys = testnet::proper_ed_keys;
            result.bls_keys = testnet::bls_keys;
            result.conv_ratio = testnet::conv_ratio;
            result.transition_bonus = testnet::transition_bonus;
            break;

        case network_type::DEVNET:
            result.addresses = devnet::addresses;
            result.proper_ed_keys = devnet::proper_ed_keys;
            result.bls_keys = devnet::bls_keys;
            result.conv_ratio = devnet::conv_ratio;
            result.transition_bonus = devnet::transition_bonus;
            break;

        case network_type::STAGENET:  /*FALLTHRU*/
        case network_type::LOCALDEV:  /*FALLTHRU*/
        case network_type::FAKECHAIN: /*FALLTHRU*/
        case network_type::UNDEFINED: /*FALLTHRU*/
        case network_type::MAINNET:
            result.addresses = mainnet::addresses;
            result.proper_ed_keys = mainnet::proper_ed_keys;
            result.bls_keys = mainnet::bls_keys;
            result.conv_ratio = mainnet::conv_ratio;
            result.transition_bonus = mainnet::transition_bonus;
            break;
    }

    if (mocknet_is_forking(snl_state.height) || mocknet_has_forked(snl_state.height)) {
        oxen::log::info(
                globallogcat,
                fg(fmt::terminal_color::yellow) | fmt::emphasis::bold,
                "Mocknet generating mock transition data to the SESH network");

        result.addresses.clear();
        result.proper_ed_keys.clear();
        result.bls_keys.clear();

        uint64_t next_eth_addr = 0;
        uint64_t next_bls_key = 0;
        result.conv_ratio = {120, 1};  // X Oxen per Y SESH
        for (auto it : snl_state.service_nodes_infos) {
            std::shared_ptr<const service_nodes::service_node_info> sn_info = it.second;

            // NOTE: Build the Oxen -> Eth address
            for (auto contrib_it : sn_info->contributors) {
                std::string cn_address = cryptonote::get_account_address_as_str(net, 0, contrib_it.address);
                if (result.addresses.count(cn_address))
                    continue;
                eth::address eth_address = {};
                std::memcpy(eth_address.data(), &next_eth_addr, sizeof(next_eth_addr));
                next_eth_addr++;
                result.addresses[cn_address] = eth_address;
            }

            // NOTE: Build the Ed -> BLS key mapping
            {
                crypto::ed25519_public_key ed_key = {};
                std::memcpy(ed_key.data(), &it.first, sizeof(it.first));

                eth::bls_public_key bls_key = {};
                std::memcpy(bls_key.data(), &next_bls_key, sizeof(next_bls_key));
                next_bls_key++;

                result.bls_keys[ed_key] = bls_key;
            }
        }
    }

    return result;
}

static void dump_eth_addr_to_sesh_allocation(const std::unordered_map<eth::address, uint64_t>& unallocated)
{
    oxen::log::debug(logcat, "Writing SESH->ETH allocation to disk");
    auto file = fmt::output_file("sesh_eth_addr_allocation.csv");
    file.print("eth_addr,sesh_tokens\n");
    size_t index = 0;
    for (auto it : unallocated)
        file.print("0xSCRAMBLED_{},{}\n", index++, it.second);
}

struct node_zombie {
    bool missing_ed25519_key;
    bool missing_bls_key;
    bool partially_funded;
    bool contributor_not_registered_for_swap;
    bool insufficient_sesh;
    uint64_t tokens_allocated;
};

struct node_transition {
    crypto::public_key old_pkey;
    crypto::public_key pkey;
    std::shared_ptr<service_nodes::service_node_info> sn_info;
    node_zombie zombie;
};

static void dump_sn_transition_outcome(const transition_context& context, std::span<node_transition> node_list)
{
    oxen::log::debug(logcat, "Writing SN SESH transition outcome to disk");
    auto file = fmt::output_file("sn_sesh_transition_outcome.csv");
    file.print("staking_requirement,{}\n", context.staking_requirement);
    file.print("conversion_ratio,{} OXEN/{} SESH\n", context.conv_ratio.first, context.conv_ratio.second);
    file.print("old_pkey,pkey,tokens_allocated,transitioned,missing_ed25519_key,missing_bls_key,partially_funded,contributor_not_registered_for_swap,insufficient_sesh\n");

    for (auto it : node_list) {
        bool transitioned = it.zombie.tokens_allocated >= context.staking_requirement;
        file.print(
                "{}," // old_pkey
                "{}," // pkey
                "{}," // tokens_allocated
                "{}," // transitioned
                "{}," // only_has_monero_key
                "{}," // missing_bls_key
                "{}," // partially_funded
                "{}," // contributor_not_registered_for_swap
                "{}," // insufficient_sesh
                "\n",
                it.old_pkey,
                it.pkey,
                it.zombie.tokens_allocated,
                transitioned,
                it.zombie.missing_ed25519_key,
                it.zombie.missing_bls_key,
                it.zombie.partially_funded,
                it.zombie.contributor_not_registered_for_swap,
                it.zombie.insufficient_sesh);
    }
}

void transition(
        service_nodes::service_node_list::state_t& snl_state,
        cryptonote::BlockchainSQLite& sql,
        network_type net) {

    transition_context context = get_transition_context(net, snl_state);

    auto address_info_from_str = [](network_type network, const std::string& addr) {
        cryptonote::address_parse_info api;
        if (!get_account_address_from_str(api, network, addr) || api.has_payment_id ||
            api.is_subaddress)
            throw std::runtime_error{fmt::format(
                    "Unable to perform SENT transition: batching database contains invalid, "
                    "unparseable, or non-OXEN address '{}'",
                    addr)};
        return api;
    };
    const auto& conv_ratio = context.conv_ratio;

    const auto& unparsed_sent_addrs = context.addresses;
    log::debug(logcat, "oxen -> sent addr map size: {}", unparsed_sent_addrs.size());
    std::unordered_map<cryptonote::account_public_address, eth::address> sent_addrs;
    for (const auto& [o, s] : unparsed_sent_addrs) {
        auto parsed_addr_info = address_info_from_str(net, o);
        sent_addrs[parsed_addr_info.address] = s;
    }

    const auto& remap_ed_keys = context.proper_ed_keys;
    const auto& node_bls_keys = context.bls_keys;

    auto oxen_to_sent = [&conv_ratio](uint64_t oxen) {
        return oxen * conv_ratio.first / conv_ratio.second;
    };

    // We start out by finding the total amount of SENT owed to each ETH address: starting from the
    // SN bonus, then we'll add converted amounts for any batched rewards, then convert existing
    // stakes.  Then, once we know each address's total, we'll go back and try to re-fill as many
    // SNs as we can from the unallocated amounts.
    std::unordered_map<eth::address, uint64_t> unallocated = context.transition_bonus;
    for (const auto& [eth, amount] : unallocated) {
        log::debug(logcat, "transition bonuses:");
        log::debug(logcat, "\tSENT {} has {}", eth, amount);
    }

    // Convert any balances for registered accounts in the batching db, removing it from the
    // batching db.  (If there is SENT left over at the end we'll put it back in, but under the
    // converted SENT address).

    cryptonote::block_payments converted_rewards;
    auto [accrued_addr, accrued_value] = sql.get_all_accrued_rewards();
    assert(accrued_addr.size() == accrued_value.size());
    for (size_t i = 0; i < accrued_addr.size(); i++) {
        auto& addr = accrued_addr[i];
        auto& val = accrued_value[i];

        auto api = address_info_from_str(net, addr);
        const auto& oxen_addr = api.address;

        auto it = sent_addrs.find(oxen_addr);
        if (it == sent_addrs.end())
            continue;

        const auto& eth_addr = it->second;
        unallocated[eth_addr] += oxen_to_sent(val);
        log::debug(
                logcat,
                "oxen -> sent ({} -> {}) accrued unpaid oxen rewards: {}",
                addr,
                eth_addr,
                val);
        converted_rewards[oxen_addr] = val;
    }

    for (const auto& [eth, amount] : unallocated) {
        log::debug(logcat, "SENT {} has unallocated {}", eth, amount);
    }

    std::vector<crypto::key_image> permanent_stakes;
    // Pass one: convert all stakes (of registered users) to our SENT bucket.  We'll leave the
    // values in place for now; we come back and update everything later.
    for (const auto& [pubkey, info] : snl_state.service_nodes_infos) {
        auto& old_stakes = info->contributors;
        for (auto& contributor : old_stakes) {
            auto addr = cryptonote::get_account_address_as_str(net, false, contributor.address);
            if (auto it = sent_addrs.find(contributor.address); it != sent_addrs.end()) {
                // Although the sum of .locked_contributions.amount is *usually* the same as
                // .amount, it's possible for a small over-contribution to have been accepted which
                // would show up in the locked amounts but not the aggregate amount (for example: if
                // a SN has 123.456 available and someone contributes 123.5)
                uint64_t total = 0;
                for (const auto& lc : contributor.locked_contributions) {
                    permanent_stakes.push_back(lc.key_image);
                    total += lc.amount;
                }
                unallocated[it->second] += oxen_to_sent(total);
                log::debug(
                        logcat,
                        "old stake from {} of amount {} -> SENT {} of amount {}, SENT balance {}",
                        addr,
                        total,
                        it->second,
                        oxen_to_sent(total),
                        unallocated[it->second]);
            } else
                log::debug(logcat, "no SENT address for OXEN wallet {}", addr);
        }
    }

    if (mocknet_has_forked(snl_state.height) || mocknet_is_forking(snl_state.height)) {
        dump_eth_addr_to_sesh_allocation(unallocated);
    }

    // We consider service nodes from oldest to most recent, replacing OXEN allocations with the
    // same proportion of SENT allocations for each contributor, and replacing contributor addresses
    // with their SENT addresses.
    //
    // By going oldest to newest we prioritize nodes that have been online the longest, which means
    // they are more likely to be good, solid nodes, and (for multi-contributor nodes) the
    // contributors appear to be happy with them since they haven't unstaked. (We could sort just
    // about any way we like, but this seems a reasonable choice).  In the case of two equal age
    // nodes, we break the tie by sorting by pubkey.
    //
    // As we transition we first figure out whether a node can survive:
    // - all contributors (including the operator) must be registered for the swap
    // - all contributors (including the operator) must have enough so-far unallocated SENT to be
    //   able to commit the same proportional amount of SENT (e.g. a staker with 31% of the OXEN
    //   staking contribution needs to have 31% of the required SENT staking contribution).
    //
    // If it can survive, we update the staking addresses to the ETH addresses, update the stakes to
    // the SENT amount, and remove that amounts from the unallocated funds bucket.
    //
    // If it can't survive (either because of unregistered contributors, or because of insufficient
    // staking funds), we mark it as a zombie, which means no contributors and a zero stake.  This
    // zombification also immediately releases any OXEN (The testing swarms will take care of
    // ejecting these off the network over the blocks after the fork).
    std::vector<std::pair<crypto::public_key, const service_nodes::service_node_info*>> sorted_sns;
    sorted_sns.reserve(snl_state.service_nodes_infos.size());
    for (const auto& [pk, sn] : snl_state.service_nodes_infos) {
        sorted_sns.emplace_back(pk, sn.get());
    }

    std::sort(sorted_sns.begin(), sorted_sns.end(), [](auto& a, auto& b) {
        return std::tie(a.second->registration_height, a.first) <
               std::tie(b.second->registration_height, b.first);
    });

    // Re-key any nodes which were keyed on an old monero-style "ed" key.  If the key is not
    // found in the `proper_ed_keys` map, it does not need remapped (i.e. is proper ed already).
    std::unordered_map<crypto::public_key, crypto::ed25519_public_key> remapped = remap_ed_keys;
    for (const auto& [key, _ignore] : sorted_sns)
        if (!remapped.contains(key))
            remapped[key] = crypto::ed25519_public_key{key};

    // This will contain our *new* list of service nodes, with only SENT contributors/stakes
    // converted from `sorted_sns`.
    std::vector<node_transition> post_transition_sns;

    std::unordered_set<crypto::public_key> zombies;

    const auto& staking_requirement = context.staking_requirement;
    const auto& staking_ratio = context.staking_ratio;
    const auto& oxen_staking_requirement = context.oxen_staking_requirement;

    for (const auto& [pk, sni] : sorted_sns) {
        bool zombie = false;
        node_zombie zombieness = {};

        // We have 5 exceptions to the 15k staking requirement on the OXEN mainnet, registered
        // continuously since before the staking requirement was fixed at 15k (HF16, i.e. Oxen 8).
        std::optional<std::pair<uint32_t, uint32_t>> extra_ratio;
        if (sni->staking_requirement > oxen_staking_requirement) {
            // +1 because we want this ratio to err on the size of being too small so that we are
            // guaranteed to have a sum of contributions at the end that are <= the required amount.
            // This is computed in tenths of an OXEN to ensure we won't overflow when applying the
            // ratio while still being able to get reasonably close to the precise number.
            extra_ratio.emplace(
                    oxen_staking_requirement / 100'000'000,
                    sni->staking_requirement / 100'000'000 + 1);

            // The maximum OXEN contribution amount we have is just under 17500, which means in the
            // code below we could (as an intermediate step) end up calculating up to just under 7/6
            // of the SENT staking requirement; thus we want to ensure that when we multiply such a
            // value by extra_ratio.first, we won't overflow:
            static_assert(
                    std::numeric_limits<uint64_t>::max() / 15000'0 >
                    (SENT_STAKING_REQUIREMENT * 7 + 5) / 6 /* ceiling division */);
        }

        bool bls_ok = true;

        // Nodes with old monero-style key which did not broadcast a proper ed25519 key
        // shouldn't make it this far, but check just in case and zombie if so
        if (!remapped.contains(pk)) {
            log::debug(
                    logcat,
                    "Node {} (monero-ed) not transitioning because there is no mapped proper "
                    "ed25519 key",
                    pk);
            zombie = true;
            bls_ok = false;
            zombieness.missing_ed25519_key = true;
        }

        // Nodes with no ed->bls key mapping do not get transitioned
        if (!node_bls_keys.contains(remapped[pk])) {
            log::debug(
                    logcat,
                    "Node {} (ed) not transitioning because there is no mapped bls key",
                    remapped[pk]);
            zombie = true;
            bls_ok = false;
            zombieness.missing_bls_key = true;
        }

        // Partially funded nodes at the time of transition just get dropped and will have to be
        // re-registered via a SENT multi-contributor contract.
        if (!sni->is_fully_funded()) {
            log::debug(
                    logcat,
                    "Node {} (ed) not transitioning because it is not fully funded",
                    remapped[pk]);
            zombie = true;
            zombieness.partially_funded = true;
        }

        // Now compute how much SENT must be staked in order to maintain the same relative stake in
        // this SN.  E.g. if you had a 21% stake before (3150 OXEN) and the SENT staking requirement
        // is 20k then your SENT stake in this node will become 21% of 20k (4200 SENT).
        std::unordered_map<eth::address, uint64_t> sent_stake;
        for (auto& contributor : sni->contributors) {
            auto addr = cryptonote::get_account_address_as_str(net, false, contributor.address);
            auto it = sent_addrs.find(contributor.address);
            if (it == sent_addrs.end()) {
                log::debug(logcat, "no sent addr for oxen wallet {}", addr);
                zombie = true;
                zombieness.contributor_not_registered_for_swap = true;
                continue;
            }

            uint64_t sent_required =
                    contributor.amount * staking_ratio.first / staking_ratio.second;
            if (extra_ratio)
                sent_required = sent_required * extra_ratio->first / extra_ratio->second;

            sent_stake[it->second] += sent_required;
            log::debug(
                    logcat, "have {} from SENT {} for node {}", sent_required, it->second, pk);

            // Sum up the total amount of that were allocated to this node (accounting only those
            // that were eligible).
            zombieness.tokens_allocated += sent_required;
        }

        eth::address sn_op = crypto::null<eth::address>;

        // Make sure all the contributors have enough unallocated SENT to actually carry over the
        // stake; if any don't then the SN becomes a zombie to be deregistered.
        if (!zombie) {
            sn_op = sent_addrs.at(sni->operator_address);

            // Our truncating integer divisions above will likely have slightly undercalculated some
            // of the staking requirements, so add the missing atomic amount to the operator
            // requirement
            uint64_t deficit = staking_requirement;
            for (const auto& [eth, reqd] : sent_stake) {
                assert(reqd <= staking_requirement);
                deficit -= reqd;
            }
            if (deficit) {
                sent_stake[sn_op] += deficit;
                zombieness.tokens_allocated += deficit;
            }

            std::unordered_map<eth::address, uint64_t> allocated;
            for (const auto& [eth, reqd] : sent_stake) {
                assert(unallocated.count(eth));
                if (unallocated[eth] - allocated[eth] < reqd) {
                    log::debug(
                            logcat,
                            "insufficient sent from {}, have {} need {}",
                            eth,
                            unallocated[eth] - allocated[eth],
                            reqd);
                    zombie = true;
                    zombieness.insufficient_sesh = true;
                    break;
                }
                allocated[eth] += reqd;
            }

            if (!zombie) {
                for (auto& [eth, amt] : allocated) {
                    unallocated[eth] -= amt;
                    log::debug(
                            logcat,
                            "allocated {} from SENT {} for node {}, new SENT balance {}",
                            amt,
                            eth,
                            pk,
                            unallocated[eth]);
                }
            }
        }

        // We're going to rewrite the service node info now *regardless* of whether it's a zombie or
        // not, but if a zombie we're deliberately writing data that will get it kicked out shortly
        // after the fork.
        auto new_state = std::make_shared<service_nodes::service_node_info>(*sni);
        auto& sn = *new_state;

        if (!zombie) {
            auto& stakers = sn.contributors;
            stakers.clear();

            sn.total_contributed = staking_requirement;
            sn.total_reserved = staking_requirement;
            sn.staking_requirement = staking_requirement;

            // Insert the operator first, then after that we sort by stake size descending, and then
            // address to break ties of equal-stake stakers.
            {
                auto it = sent_stake.find(sn_op);
                assert(it != sent_stake.end());
                auto& stake = stakers.emplace_back();
                stake.ethereum_address = it->first;
                stake.ethereum_beneficiary = it->first;
                stake.amount = it->second;
                sn.operator_ethereum_address = it->first;
                sent_stake.erase(it);
            }
            std::vector<std::pair<eth::address, uint64_t>> stakes_desc{
                    sent_stake.begin(), sent_stake.end()};
            std::sort(stakes_desc.begin(), stakes_desc.end(), [](auto& a, auto& b) {
                if (a.second != b.second)
                    return a.second > b.second;  // a comes first if the *value* is larger
                return a.first <
                       b.first;  // same value: a comes first if the *address* is "smaller"
            });
            for (const auto& [address, amount] : stakes_desc) {
                auto& stake = stakers.emplace_back();
                stake.ethereum_address = address;
                stake.ethereum_beneficiary = address;
                stake.amount = amount;
            }

            sn.bls_public_key =
                    node_bls_keys.at(remapped[pk]);  // operator [] and const being weird
            post_transition_sns.emplace_back(pk, crypto::public_key{remapped[pk]}, new_state, zombieness);

        } else {
            // This SN is a zombie, i.e. its dying and will get deregged shortly after the fork.
            // We're leaving it technically registered, but just a husk: it has no contributors and
            // a 0 staking requirement/total.

            sn.total_contributed = 0;
            sn.total_reserved = 0;
            sn.staking_requirement = 0;
            sn.contributors.clear();

            // if we made it this far and the node has supplied a bls key, we set it so that until
            // the node is removed we can still at least try to request a bls signature from it,
            // as its bls key will be in the contract until it is removed.
            if (bls_ok)
                sn.bls_public_key = node_bls_keys.at(remapped[pk]);
            else
                sn.bls_public_key = crypto::null<eth::bls_public_key>;

            post_transition_sns.emplace_back(pk, pk, new_state, zombieness);
        }
    }

    // Any yet-unallocated SENT balance goes in the rewards db to be claimed
    // All OXEN rewards are wiped first
    sql.set_rewards_hf21(unallocated);

    // First, clear the old key image blacklist so we don't leave unconverted stakes locked
    // any longer than necessary (and can re-use the blacklist for perma-locks)
    //
    // Then *permanently* blacklist the key images of all converted stakes (but not
    // unconverted ones), so that you can't go back to the OXEN wallet and then convert
    // them through the external SENT conversion process.
    snl_state.key_image_blacklist.clear();
    for (const crypto::key_image& img : permanent_stakes) {
        auto& bl_entry = snl_state.key_image_blacklist.emplace_back();
        bl_entry.key_image = img;
    }

    if (snl_state.service_nodes_infos.size() != post_transition_sns.size())
        throw std::runtime_error{"post-transition should have same number of service_node_infos!"};

    if (mocknet_is_forking(snl_state.height) || mocknet_has_forked(snl_state.height)) {
        dump_sn_transition_outcome(context, post_transition_sns);
    }

    snl_state.service_nodes_infos.clear();
    for (auto& it : post_transition_sns)
        snl_state.service_nodes_infos[it.pkey] = std::move(it.sn_info);

}

}  // namespace oxen::sent
