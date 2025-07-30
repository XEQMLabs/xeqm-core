#pragma once

#include "mainnet.h"

namespace cryptonote::config::testnet {

inline constexpr std::array seeds = {
        "37.27.236.229:35602"sv,  // public.session.foundation
        "104.243.40.38:35619"sv,  // angus.oxen.io
};

inline constexpr network_config config{
        .NETWORK_TYPE = network_type::TESTNET,
        .DEFAULT_CONFIG_SUBDIR = "testnet"sv,
        .HEIGHT_ESTIMATE_HEIGHT = 0,  // Reset for new chain
        .HEIGHT_ESTIMATE_TIMESTAMP = 0,  // Start from beginning
        .PUBLIC_ADDRESS_BASE58_PREFIX = 0x1c1eb4,
        .PUBLIC_INTEGRATED_ADDRESS_BASE58_PREFIX = 0x179eb4,
        .PUBLIC_SUBADDRESS_BASE58_PREFIX = 0x0f1eb4,
        .P2P_DEFAULT_PORT = 38156,
        .RPC_DEFAULT_PORT = 38157,
        .QNET_DEFAULT_PORT = 38159,
        .NETWORK_ID = {{
                0x54,
                0x45,
                0x53,
                0x54,  // "TEST"
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
        }},
        .P2P_SEED_NODES = {},
        .GENESIS_TX =
                "021e01ff00018080c9db97f4fb270278fe7eed60deb57ea863d65690c40abc5ff09505704f36ba94285a02b9e15f6942014c8f90cc270b3e4e154003ecf48f21b5762e57c52cc70df9c65502e4921cd97872000000000000000000000000000000000000000000000000000000000000000000"sv,
        .GENESIS_NONCE = 12345,
        .GOVERNANCE_REWARD_INTERVAL = 2000min,
        .GOVERNANCE_WALLET_ADDRESS =
                {
                        "XEQT8QP2BWFevNjsrvAqipQdCF7C6WdXs5BPWfa2vZRH4h1ZfuRr9wtds5gAwCGqoAd5psteQiTwdH17Fe3Vb7G25BkRFEo2MC",  // HF10
                },
        .UPTIME_PROOF_TOLERANCE = mainnet::config.UPTIME_PROOF_TOLERANCE,
        .UPTIME_PROOF_STARTUP_DELAY = mainnet::config.UPTIME_PROOF_STARTUP_DELAY,
        .UPTIME_PROOF_CHECK_INTERVAL = mainnet::config.UPTIME_PROOF_CHECK_INTERVAL,
        // Testnet uptime proofs are 6x faster than mainnet (devnet config also uses these)
        .UPTIME_PROOF_FREQUENCY = 10min,
        .UPTIME_PROOF_VALIDITY = 21min,
        .HAVE_STORAGE_AND_LOKINET = false,  // Disable storage & lokinet
        .TARGET_BLOCK_TIME = mainnet::config.TARGET_BLOCK_TIME,
        .PULSE_STAGE_TIMEOUT = mainnet::config.PULSE_STAGE_TIMEOUT,
        .PULSE_ROUND_TIMEOUT = mainnet::config.PULSE_ROUND_TIMEOUT,
        .PULSE_MAX_START_ADJUSTMENT = mainnet::config.PULSE_MAX_START_ADJUSTMENT,
        .PULSE_MIN_SERVICE_NODES = 12,  // == pulse quorum size
        .BATCHING_INTERVAL = 20,
        .MIN_BATCH_PAYMENT_AMOUNT = mainnet::config.MIN_BATCH_PAYMENT_AMOUNT,
        .LIMIT_BATCH_OUTPUTS = mainnet::config.LIMIT_BATCH_OUTPUTS,
        .SERVICE_NODE_PAYABLE_AFTER_BLOCKS = 4,
        .DEREGISTRATION_LOCK_DURATION = 48h,
        .UNLOCK_DURATION = 24h,
        .HARDFORK_DEREGISTRATION_GRACE_PERIOD =
                mainnet::config.HARDFORK_DEREGISTRATION_GRACE_PERIOD,
        .HISTORY_ARCHIVE_INTERVAL = mainnet::config.HISTORY_ARCHIVE_INTERVAL,
        .HISTORY_ARCHIVE_KEEP_WINDOW = mainnet::config.HISTORY_ARCHIVE_KEEP_WINDOW,
        .HISTORY_RECENT_KEEP_WINDOW = mainnet::config.HISTORY_RECENT_KEEP_WINDOW,
        // Much shorter than mainnet so that you can test this more easily.
        .ETH_EXIT_BUFFER = 1h / mainnet::config.TARGET_BLOCK_TIME,
        .ETH_DEREG_BUFFER = 1h / mainnet::config.TARGET_BLOCK_TIME,
        // FIXME!
        .ETHEREUM_CHAIN_ID = 421614,  // Arbitrum Sepolia
        .ETHEREUM_REWARDS_CONTRACT = "0x0B5C58A27A41D5fE3FF83d74060d761D7dDDc1D2",
        .ETHEREUM_POOL_CONTRACT = "0x8D69Bb9D7b03993234bfd221aCB391Db597a920a",
        // Sepolia arbitrum sometimes slows down below the typical 250ms seen on mainnet, so for
        // testnet/devnet we shorten this to a quarter compared to mainnet:
        .L2_REWARD_POOL_UPDATE_BLOCKS = mainnet::config.L2_REWARD_POOL_UPDATE_BLOCKS / 4,
        .L2_TRACKER_SAFE_BLOCKS = mainnet::config.L2_TRACKER_SAFE_BLOCKS,
        // arb sepolia blocks are (sometimes) slower than mainnet, so reduce this a bit so that
        // we're probably still somewhere in the 1-2 hour range:
        .L2_NODE_LIST_PURGE_BLOCKS = mainnet::config.L2_NODE_LIST_PURGE_BLOCKS / 2,
        .L2_NODE_LIST_PURGE_MIN_OXEN_AGE = mainnet::config.L2_NODE_LIST_PURGE_MIN_OXEN_AGE,
        .DEFAULT_STAKING_URL = ""sv,
};
}  // namespace cryptonote::config::testnet
