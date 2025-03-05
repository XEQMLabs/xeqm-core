#pragma once

#include "../cryptonote_config.h"
#include "network_config.h"

using namespace std::literals;
namespace cryptonote::config::mainnet {

inline constexpr auto TARGET_BLOCK_TIME = 1min;
inline constexpr network_config config{
        .NETWORK_TYPE = network_type::MAINNET,
        .DEFAULT_CONFIG_SUBDIR = ""sv,
        .HEIGHT_ESTIMATE_HEIGHT = 0,
        .HEIGHT_ESTIMATE_TIMESTAMP = 1710140000,
        .PUBLIC_ADDRESS_BASE58_PREFIX = 0x04F270,
        .PUBLIC_INTEGRATED_ADDRESS_BASE58_PREFIX = 0x5c134b,
        .PUBLIC_SUBADDRESS_BASE58_PREFIX = 0x5c134c,
        .P2P_DEFAULT_PORT = 22022,
        .RPC_DEFAULT_PORT = 22023,
        .QNET_DEFAULT_PORT = 22025,
        .NETWORK_ID =
                {{0x45, 0x51, 0x55, 0x49, 0x4c, 0x49, 0x42, 0x52,  // "EQUILIBR"
                       0x49, 0x41, 0x4e, 0x45, 0x54, 0x57, 0x4f, 0x52}},
        .GENESIS_TX =
                "020102ff000180808081b1d4808081b1d4808081b1d4c0c7c2869a37e1c1c3c6c2869a37e1c1c3c6c2869a37e1"
                "c1c3b8e4a1cec6b7a2b9486d361c5c8f25f4c3c6b7a2b9486d361c5c8f25f4c3c6b7a2b9486d361c5c8f25f4"sv,
        .GENESIS_NONCE = 10000,
        .GOVERNANCE_REWARD_INTERVAL = 24h,
        .GOVERNANCE_WALLET_ADDRESS =
                {
                        "LCFxT37LAogDn1jLQKf4y7aAqfi21DjovX9qyijaLYQSdrxY1U5VGcnMJMjWrD9RhjeK5Lym67"
                        "wZ73uh9AujXLQ1RKmXEyL",  // HF7-10
                        "LDBEN6Ut4NkMwyaXWZ7kBEAx8X64o6YtDhLXUP26uLHyYT4nFmcaPU2Z2fauqrhTLh4Qfr61pU"
                        "UZVLaTHqAdycETKM1STrz",  // HF11
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
        .HISTORY_RECENT_KEEP_WINDOW = 360,
        .ETH_EXIT_BUFFER = 7 * 24h / TARGET_BLOCK_TIME,
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

// Add seed nodes configuration
inline constexpr std::array<const char*, 5> SEED_NODES = {
    "seed1.equilibria.network:22022",
    "seed2.equilibria.network:22022", 
    "seed3.equilibria.network:22022",
    "seed4.equilibria.network:22022",
    "seed5.equilibria.network:22022"
};

inline constexpr std::array<const char*, 3> CHECKPOINT_NODES = {
    "checkpoint1.equilibria.network:22022",
    "checkpoint2.equilibria.network:22022",
    "checkpoint3.equilibria.network:22022"
};
}  // namespace cryptonote::config::mainnet
