# RandomX / PoW-fallback retirement plan

Status: draft / proposal
Date: 2026-10-19
Scope: mainnet consensus and node operation
Related: `feature::pulse_recovery` (hf22 snode_revision 1, activated mainnet height 236920)

## Summary

XEQM is a Pulse chain. RandomX proof-of-work has only ever been a fallback for when a Pulse
round fails to produce a block, never the normal path. The rev1 signed fallback
(`feature::pulse_recovery`) replaced the PoW fallback as the recovery mechanism, so RandomX no
longer has any role in producing or accepting new blocks.

The important finding behind this plan is that the consensus half of retiring RandomX has
already shipped. Since mainnet height 236920, a non-Pulse block must carry an authorized
fallback-miner signature to be valid, and `Blockchain::verify_block_pow` never reaches the
RandomX path for such a block. A PoW-only block cannot be accepted today. What remains is not a
new consensus rule but code and operational cleanup, plus solving the one thing that still binds
us to RandomX, namely verification of the historical PoW blocks already on the chain.

This document lays out the staged path to remove RandomX, what each stage requires, what is safe
to do without a fork, and the traps to avoid.

## How block production and validation work today

Three producer types exist, distinguished by block contents and the height-to-hardfork mapping
rather than by any explicit "block type" field (`src/cryptonote_basic/cryptonote_basic.h:502-525`):

- **Pulse block.** `block::has_pulse()` is true. The normal path. No PoW.
- **Signed fallback block (rev1).** `has_pulse()` is false and the block carries exactly one
  signature at `FALLBACK_MINER_VOTER_INDEX` (0xFFFF) from an authorized key. No PoW.
- **PoW block (historical, or hf22 rev0 fallback).** `has_pulse()` is false, validated by the
  `nonce` meeting the RandomX difficulty target via `check_hash`.

The central PoW gate is `Blockchain::verify_block_pow` (`src/cryptonote_core/blockchain.cpp:5216-5339`).
Its first branch (`blockchain.cpp:5235-5251`) is the signed-fallback short-circuit:

```cpp
if (auto [blk_ver, blk_rev] = get_network_version_revision(...);
    !blk.has_pulse() && feature::pulse_recovery(blk_ver, blk_rev))
    // verify via service_nodes::verify_fallback_miner_signature(...) and return, no PoW
```

For any block whose `(version, revision)` satisfies `pulse_recovery` (everything at or above
mainnet height 236920), a non-Pulse block is verified purely by signature and the RandomX path is
never reached. Below that height, a non-Pulse block falls through to the RandomX longhash and
`check_hash` (`blockchain.cpp:5309-5336`, `src/cryptonote_core/cryptonote_tx_utils.cpp:1357-1392`).

Separately, `service_nodes::verify_block_components` (`src/cryptonote_core/service_node_list.cpp:2864-2954`)
has required an authorized fallback signature on every non-Pulse block since hf22_sn_policy
(mainnet height 219120). So from 219120 a PoW block also needed the signature, and from 236920 it
needs only the signature. New unsigned PoW blocks have not been acceptable for a long time.

The RandomX gate is `feature::pulse_recovery(version, snode_revision)`
(`src/cryptonote_config.h:317-320`). It was shipped as a `snode_revision` bump of hf22 rather than a
new major version, deliberately, to keep the on-chain major version at 22 and avoid the ETH/L2
block-template path that a major bump would pull in.

## The one real constraint: historical PoW verification

RandomX cannot simply be deleted, because a node syncing from genesis must still validate every
historical non-Pulse block in the range `[hf12 height 3, pulse_recovery height 236920)`. That
range includes the genuinely PoW-mined early blocks and the hf22 rev0 PoW-fallback blocks
(roughly heights 219120 to 236920).

Today that verification is unavoidable on a fresh sync, because:

- `HARDCODED_MAINNET_CHECKPOINTS` contains only genesis; every other entry is commented out
  (`src/checkpoints/checkpoints.cpp:66-95`).
- The per-block fast check (`PER_BLOCK_CHECKPOINT`, `m_blocks_hash_check`) reads from
  `src/blocks/mainnet_blocks.dat`, which is currently 0 bytes, so the fast path is empty.

So a node today runs `rx_slow_hash` for every historical non-Pulse block. Removing RandomX
without first giving nodes another way to trust those block hashes would break full historical
sync. This is the gating dependency for any real code removal, and the stages below are ordered
around it.

A note on fork choice, because it matters once PoW is gone. Post-hf16 chain selection does not use
cumulative difficulty. It compares checkpoint count first, then a weight sum where only
`has_pulse()` blocks contribute `PULSE_BASE_WEIGHT` and everything else (signed fallback included)
contributes weight 1 (`blockchain.cpp:2750-2795`). Chain selection therefore keeps working with no
new PoW. The caveat worth tracking is that a run of signed-fallback blocks carries almost no
fork-choice weight, so two competing all-fallback branches are separated only by length and
checkpoints. That is acceptable for short recovery bursts but should be reviewed before relying on
extended fallback-only operation. This is a property of the signed fallback, not of RandomX
removal, but it becomes more relevant once PoW is no longer even a theoretical backstop.

## Staged plan

### Stage 0 (done): recovery moved off PoW

rev1 (`pulse_recovery`) activated at mainnet height 236920. Fleet PoW miners were stopped. Recovery
now runs through the signed fallback. No new PoW block is acceptable. This is the consensus
retirement, and it is already live.

### Stage 1: remove the PoW mining surface (no fork)

Nothing can accept a freshly PoW-mined block anymore, so the mining machinery is dead weight and a
source of operator confusion. This is a non-consensus change and needs no hard fork.

- Remove or disable the `START_MINING`, `STOP_MINING`, and `MINING_STATUS` RPC endpoints
  (`src/rpc/core_rpc_server.cpp:1239-1325`, defs in `core_rpc_server_commands_defs.h:452-493`).
- Remove the `--start-mining` daemon path and the miner worker threads that call RandomX
  (`src/cryptonote_basic/miner.cpp`, instance at `src/cryptonote_core/cryptonote_core.h:854`).
- Drop the fallback-miner delay/stagger logic that only applied to PoW rounds if it is no longer
  referenced by the signed path.

Keep, do not remove:

- `--fallback-signer-key-file` (old name `--fallback-miner-key-file` still accepted) and the key
  loading, because the **signed** fallback still uses that
  key to sign blocks in `core::handle_block_found` (`cryptonote_core.cpp:2567-2600`). This flag is
  no longer about PoW.
- `miner::find_nonce_for_given_block`, which is used for genesis generation
  (`cryptonote_tx_utils.cpp:1300-1305`, `tools/generate_genesis.cpp`). Genesis is computed once and
  this is not a running-node concern, but the function should not be deleted blindly.

This stage removes the operator-facing PoW surface while leaving RandomX verification intact.

### Stage 2: give nodes a non-PoW way to trust history (enables everything after)

This is the real unblock. Populate the existing, already-supported per-block hash check so a fresh
node can validate historical blocks by hash instead of by replaying RandomX.

- Generate and commit `src/blocks/mainnet_blocks.dat` covering at least heights `[0, 236920]` (the
  full RandomX range plus margin). The loader and fast-path already exist
  (`blockchain.cpp:5285-5307`, `blocks.cpp.in`/`blocks.h`); the file is simply empty today.
- Optionally also add hardcoded checkpoints at coarse intervals in
  `HARDCODED_MAINNET_CHECKPOINTS` for defense in depth.

Important: this is a hash check, not a blind skip. Do **not** reintroduce the old behavior of
skipping PoW inside the service-node checkpoint zone. That was deliberately removed as "very
dangerous" (see the comment at `blockchain.cpp:5276-5281`), because it let a node accept
pre-checkpoint blocks without checking work at all. The per-block `.dat` mechanism is safe because
it verifies each block hash against a value baked into the binary, so an attacker cannot substitute
a different history.

With `mainnet_blocks.dat` populated, a fresh node stops invoking RandomX for the covered range on
its own. RandomX is still compiled and still runs only if a block hash is not in the table, which
preserves correctness for anyone who wants a from-scratch full verify.

### Stage 3 (optional, defense in depth): make the no-PoW rule explicit

Consensus already rejects new PoW, so this stage is optional. If we want the intent written into
the code and a clean hook for later removal, add a `pow_fallback_retired(version, snode_revision)`
predicate in the same style as `pulse_recovery` (`cryptonote_config.h:317-320`) and have
`verify_block_pow` treat reaching the RandomX branch at or above the retirement revision as a hard
error rather than a path that merely never triggers. Ship it as another hf22 `snode_revision` bump
(rev2) with a new row in each `*_hard_forks` table (`src/cryptonote_basic/hardfork.cpp`), matching
how rev1 was delivered. This changes nothing a well-behaved node does; it only turns an
unreachable path into an asserted-impossible one.

If we do this, respect the hf-enum constraints below. If we decide the existing behavior is clear
enough, this stage can be skipped entirely.

### Stage 4: height-gate RandomX invocation behind the checkpoints

Once Stage 2 is deployed and soaked, change the verification path so RandomX is only invoked when a
block hash is genuinely absent from the committed checkpoint table, and make the normal synced path
never touch RandomX. The code already prefers the hash table when present; this stage is about
confirming the RandomX allocation and cache init (`src/crypto/rx-slow-hash.c`) are not entered on a
normal node and can stay fully idle. The idle-cache release already frees RandomX caches after 600s
of no PoW verification (`rx_release_idle_cache`, `rx-slow-hash.c:336-363`, scheduled at
`cryptonote_core.cpp:1359-1361`), so a node that never verifies PoW should hold no RandomX memory.

### Stage 5 (optional, possibly never): remove the RandomX code

Only after Stages 2 to 4 are proven can the library be removed, and even then it is a judgment
call. Full removal means:

- Drop the `external/randomx` submodule and `src/crypto/rx-slow-hash.c`.
- Remove RandomX from every link target, not just the daemon. It is also pulled into wallet builds
  (`src/wallet/api/CMakeLists.txt`, `src/wallet3/CMakeLists.txt`).
- Replace the historical-block path so that below the retirement height a node either trusts the
  committed hash table or refuses to run without it.

The recommendation is to defer Stage 5 indefinitely. Keeping RandomX compiled but dormant costs
almost nothing (the idle-cache release reclaims its memory), and it preserves the ability to do a
trustless from-genesis verify. Deleting it removes that ability permanently and buys little. Revisit
only if the maintenance burden of carrying the submodule becomes real.

## Risks and traps

These come from the code map and must be respected by whoever implements the stages.

1. **hf-enum renumbering trap.** The `hf` enum has static_asserts guarding the ETH fork numbering
   (`src/cryptonote_config.h:287,327-332`). Any new enumerator must be appended after the existing
   ones (before `_next`) and the `hf_max` static_assert bumped. Inserting one in the wrong place
   silently shifts every `>=` version comparison and would need auditing against ~40 ETH_BLS call
   sites. This only applies if Stage 3 adds a new enumerator; the `snode_revision`-bump approach
   avoids it.
2. **RandomX is linked into wallets too.** Any removal (Stage 5) must account for the wallet API and
   wallet3 link targets, not only the daemon and crypto lib.
3. **Do not reuse the checkpoint-zone PoW skip.** The safe mechanism for Stage 2 is the per-block
   hash table (`mainnet_blocks.dat`), which verifies each hash. The old zone-based skip was removed
   as dangerous (`blockchain.cpp:5276-5281`); do not bring it back.
4. **Signed-fallback fork-choice weight.** Signed-fallback blocks contribute weight 1 to fork choice
   versus `PULSE_BASE_WEIGHT` for Pulse blocks (`blockchain.cpp:2772-2795`). Confirm this is
   acceptable for any scenario of extended fallback-only operation before treating the signed
   fallback as the sole backstop.
5. **Imprecise comment.** The comment near `blockchain.cpp:5231-5232` says fallback cumulative
   difficulty stays `PULSE_FIXED_DIFFICULTY`, but the stored value is actually `next_difficulty_v2`
   (`blockchain.cpp:5540,5752`, `src/blockchain_db/lmdb/db_lmdb.cpp:4634-4643`). Harmless today
   because post-hf16 fork choice ignores cumulative difficulty, but do not rely on that comment if
   fork choice is ever revisited.

## Recommended sequencing

1. Stage 1 (remove mining surface) in the next ordinary release. Low risk, no fork, removes operator
   confusion.
2. Stage 2 (`mainnet_blocks.dat`) in the same or the following release. This is the key enabler and
   should be done well before any talk of code removal.
3. Stage 3 (explicit rev2 assertion) only if we want the belt-and-suspenders, bundled with a future
   hard fork that is happening anyway (for example alongside HF23), never as a fork on its own.
4. Stage 4 after Stage 2 has soaked across real sync and reorg activity.
5. Stage 5 deferred, revisited only if carrying RandomX becomes a real cost.

The net is that most of the value (a PoW-free operator experience and nodes that no longer replay
RandomX on sync) is reached by Stages 1 and 2 with no consensus risk, because the consensus
retirement already happened at rev1.
