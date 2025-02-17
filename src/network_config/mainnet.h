#pragma once

#include "../cryptonote_config.h"
#include "network_config.h"

using namespace std::literals;
namespace cryptonote::config::mainnet {

inline constexpr auto TARGET_BLOCK_TIME = 1min;
inline constexpr network_config config{
        .NETWORK_TYPE = network_type::MAINNET,
        .DEFAULT_CONFIG_SUBDIR = ""sv,
        .HEIGHT_ESTIMATE_HEIGHT = 32322,
        .HEIGHT_ESTIMATE_TIMESTAMP = 1710140000,
        .PUBLIC_ADDRESS_BASE58_PREFIX = 0x191eb4,
        .PUBLIC_INTEGRATED_ADDRESS_BASE58_PREFIX = 0x191eb4,
        .PUBLIC_SUBADDRESS_BASE58_PREFIX = 0x191eb4,
        .P2P_DEFAULT_PORT = 9230,
        .RPC_DEFAULT_PORT = 9231,
        .QNET_DEFAULT_PORT = 9232,
        .NETWORK_ID =
                {{0x43,
                  0x6c,
                  0x6c,
                  0x79,
                  0x6f,
                  0x75,
                  0x72,
                  0x53,
                  0x45,
                  0x4e,
                  0x54,
                  0x61,
                  0x72,
                  0x65,
                  0x62,
                  0x65}},
        .GENESIS_TX =
                "011e01ff00018080c9db97f4fb27022f5400bc9f976c15e9b045a2dd99aa4d988dfb4e15d6589e75b2ce37aafa3e5a2101c5f3513f4085c3786d2a1d5b7f449f288abb5f200987d2d35995e00fb21670a7"sv
        ,
        .GENESIS_NONCE = 12345,
        .GOVERNANCE_REWARD_INTERVAL = 24h,
        .GOVERNANCE_WALLET_ADDRESS =
                {
                        "XEQMPRuPnTa6GnWz7n7zqHZWWs9c4C9cwHCcpuPD776u3N5bdnDqsBu27VVZ9y4UARgTgnTaKxeoZK9vEa1cJnXv6JPfrLxc3W"
                },
        .UPTIME_PROOF_TOLERANCE = 5min,
        .UPTIME_PROOF_STARTUP_DELAY = 30s,
        .UPTIME_PROOF_CHECK_INTERVAL = 30s,
        .UPTIME_PROOF_FREQUENCY = 1h,
        .UPTIME_PROOF_VALIDITY = 2h + 5min,
        .HAVE_STORAGE_AND_LOKINET = true,
        .TARGET_BLOCK_TIME = TARGET_BLOCK_TIME,
        .PULSE_STAGE_TIMEOUT = 10s,
        .PULSE_ROUND_TIMEOUT = 30s,
        .PULSE_MAX_START_ADJUSTMENT = 15s,
        .PULSE_MIN_SERVICE_NODES = 25,
        .BATCHING_INTERVAL = 1440,
        .MIN_BATCH_PAYMENT_AMOUNT = 100'000'000,  // 1 OXEN (in atomic units)
        .LIMIT_BATCH_OUTPUTS = 15,
        .SERVICE_NODE_PAYABLE_AFTER_BLOCKS = 720,
        .DEREGISTRATION_LOCK_DURATION = 30 * 24h,
        .UNLOCK_DURATION = 15 * 24h,
        .HARDFORK_DEREGISTRATION_GRACE_PERIOD = 7 * 24h / TARGET_BLOCK_TIME,
        .HISTORY_ARCHIVE_INTERVAL = 10'000,
        .HISTORY_ARCHIVE_KEEP_WINDOW = 2 * 365 * 24h / TARGET_BLOCK_TIME,  // 2yrs worth
        .HISTORY_RECENT_KEEP_WINDOW = 65,
        .ETH_EXIT_BUFFER = 7 * 24h / TARGET_BLOCK_TIME,
        .ETH_DEREG_BUFFER = 7 * 24h / TARGET_BLOCK_TIME,
        .ETHEREUM_CHAIN_ID = 42161,  // Arbitrum One
        // TODO: To be set closer to mainnet TGE
        .ETHEREUM_REWARDS_CONTRACT = "",
        .ETHEREUM_POOL_CONTRACT = "",
        // Update every ~10 minutes with an Arbitrum ~250ms block time:
        .L2_REWARD_POOL_UPDATE_BLOCKS = 10min / L2_BLOCK_TIME,
        // The default is 70s behind with an Arbitrum ~250ms block time, so that pulse nodes using
        // 1min update period will work (with a few seconds for provider and request latencies).
        .L2_TRACKER_SAFE_BLOCKS = 70s / L2_BLOCK_TIME,
        // This relatively infrequent check is only for handling highly unusual cleanup cases where
        // a L2 disruption or bug between the contract and oxend results in oxend service nodes
        // state having nodes that *don't* exist in the contract (or vice versa) for some reason.
        .L2_NODE_LIST_PURGE_BLOCKS = 1h / L2_BLOCK_TIME,
        .L2_NODE_LIST_PURGE_MIN_OXEN_AGE = 24h / TARGET_BLOCK_TIME,
        .DEFAULT_STAKING_URL = "https://stake.getsession.org"sv,
};
inline constexpr std::array<const char*, 5> SEED_NODES = {
    "seed1.equilibria.network:9230",
    "seed2.equilibria.network:9230", 
    "seed3.equilibria.network:9230",
    "seed4.equilibria.network:9230",
    "seed5.equilibria.network:9230"
};

inline constexpr std::array<const char*, 3> CHECKPOINT_NODES = {
    "checkpoint1.equilibria.network:9230",
    "checkpoint2.equilibria.network:9230",
    "checkpoint3.equilibria.network:9230"
};
}  // namespace cryptonote::config::mainnet
