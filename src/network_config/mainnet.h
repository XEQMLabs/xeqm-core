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
        .NETWORK_ID = {{
                0x45,
                0x51,
                0x55,
                0x49,  // "EQUI"
                0x4c,
                0x49,
                0x42,
                0x52,  // "LIBR"
                0x49,
                0x41,
                0x4e,
                0x54,  // "IANT"
                0x00,
                0x00,
                0x00,
                0x00,
        }},
        .GENESIS_TX =
                "021e01ff00018080c9db97f4fb270278fe7eed60deb57ea863d65690c40abc5ff09505704f36ba94285a02b9e15f6942014c8f90cc270b3e4e154003ecf48f21b5762e57c52cc70df9c65502e4921cd97872000000000000000000000000000000000000000000000000000000000000000000"sv
        ,
        .GENESIS_NONCE = 12345,
        .GOVERNANCE_REWARD_INTERVAL = 24h,
        .GOVERNANCE_WALLET_ADDRESS =
                {
                        "XEQT8QP2BWFevNjsrvAqipQdCF7C6WdXs5BPWfa2vZRH4h1ZfuRr9wtds5gAwCGqoAd5psteQiTwdH17Fe3Vb7G25BkRFEo2MC"
                },
        .UPTIME_PROOF_TOLERANCE = 5min,
        .UPTIME_PROOF_STARTUP_DELAY = 30s,
        .UPTIME_PROOF_CHECK_INTERVAL = 30s,
        .UPTIME_PROOF_FREQUENCY = 10min,
        .UPTIME_PROOF_VALIDITY = 21min,
        .MAX_DEACTIVATE_PER_BLOCK = 1,
        .HAVE_STORAGE_SERVER = false,
        .HAVE_SESSION_ROUTER = false,
        .HAVE_LOKINET = false,
        .TARGET_BLOCK_TIME = TARGET_BLOCK_TIME,
        .PULSE_STAGE_TIMEOUT = 10s,
        .PULSE_ROUND_TIMEOUT = 30s,
        .PULSE_MAX_START_ADJUSTMENT = 15s,
        .PULSE_MIN_SERVICE_NODES = 12,
        .BATCHING_INTERVAL = 20,
        .MIN_BATCH_PAYMENT_AMOUNT = 100'000'000,  // 1 OXEN (in atomic units)
        .LIMIT_BATCH_OUTPUTS = 15,
        .SERVICE_NODE_PAYABLE_AFTER_BLOCKS = 4,
        .DEREGISTRATION_LOCK_DURATION = 48h,
        .UNLOCK_DURATION = 24h,
        .HARDFORK_DEREGISTRATION_GRACE_PERIOD = 5 * 24h / TARGET_BLOCK_TIME,
        .HISTORY_ARCHIVE_INTERVAL = 10'000,
        .HISTORY_ARCHIVE_KEEP_WINDOW = 2 * 365 * 24h / TARGET_BLOCK_TIME,  // 2yrs worth
        .HISTORY_RECENT_KEEP_WINDOW = 65,
        .ETH_EXIT_BUFFER = 1h / TARGET_BLOCK_TIME,
        .ETH_DEREG_BUFFER = 1h / TARGET_BLOCK_TIME,
        // .ETHEREUM_CHAIN_ID = 42161,  // Arbitrum One
        .ETHEREUM_CHAIN_ID = 421614,  // Arbitrum Sepolia
        .ETHEREUM_REWARDS_CONTRACT = "0x5FC8d32690cc91D4c39d9d3abcBD16989F875707",// "0x0B5C58A27A41D5fE3FF83d74060d761D7dDDc1D2",
        .ETHEREUM_POOL_CONTRACT = "0x9fE46736679d2D9a65F0992F2272dE9f3c7fa6e0",// "0x8D69Bb9D7b03993234bfd221aCB391Db597a920a",
        // Update every ~10 minutes with an Arbitrum ~250ms block time:
        .L2_REWARD_POOL_UPDATE_BLOCKS = 10min / config::L2_BLOCK_TIME / 4,
        // The default is 70s behind with an Arbitrum ~250ms block time, so that pulse nodes using
        // 1min update period will work (with a few seconds for provider and request latencies).
        .L2_TRACKER_SAFE_BLOCKS = 70s / config::L2_BLOCK_TIME,
        // This relatively infrequent check is only for handling highly unusual cleanup cases where
        // a L2 disruption or bug between the contract and oxend results in oxend service nodes
        // state having nodes that *don't* exist in the contract (or vice versa) for some reason.
        .L2_NODE_LIST_PURGE_BLOCKS = 1h / config::L2_BLOCK_TIME / 2,
        .L2_NODE_LIST_PURGE_MIN_OXEN_AGE = 24h / TARGET_BLOCK_TIME,
        .DEFAULT_STAKING_URL = ""sv,
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
