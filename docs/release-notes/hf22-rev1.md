# Pulse-Recovery and Memory Fix (HF22 service-node revision 1)

This release ships two changes as a revision bump of HF22, not a new hard fork: Pulse-recovery, which keeps the chain live and recovers it quickly when a Pulse quorum cannot form, and a RandomX memory fix that resolves the per-node RAM growth seen since HF22. All nodes must upgrade before the activation height, which Dom will share.

> **Action required:** Every node must upgrade to the new binary before the activation height, then restart. A node still on the old binary at the activation block will reject the new fallback blocks and fork off the network.

## Why a revision bump, not "HF23"

We kept this inside HF22 (service-node revision 0 to 1) on purpose. The "HF23" name is reserved for a separately announced feature, and staying at major version 22 keeps us below the ETH/BLS fork, which would require an external L2 provider the network does not run. It activates like a fork, meaning a set height with every node upgraded by then, but without the major-version baggage.

## Change 1: Pulse-recovery

Previously, when a Pulse round could not form a quorum, the chain fell back to slow proof-of-work mining, or stalled outright. Now an authorized node emits the next block directly as a signed, PoW-free fallback. The chain keeps moving and returns to normal Pulse within a few blocks.

Tested over repeated stall and recovery cycles on a multi-host testnet: **3 of 3 clean recoveries, zero downtime, correct block reward.**

Looking ahead, in HF23 this single authorized fallback signer will be replaced by the EXIOM Oracle, decentralizing fallback-block production.

## What has actually been causing the stalls

That fallback only fires when a Pulse quorum fails, and the main reason quorums fail is **node concentration**. When a large number of service nodes run on a small number of servers, and those servers are over-subscribed for the load (made worse by the memory issue below), the whole group becomes unreliable at once. When one server struggles, swaps, or restarts, dozens of co-located nodes drop together and repeatedly break quorums.

HF22 already took a step against this: its service-node policy prevents a single operator from occupying more than one slot in a Pulse quorum. That limits a quorum's composition, but it does not remove the underlying concentration. Quorums form reliably when nodes are spread across independent, adequately-resourced hosts. Pulse-recovery makes the symptom survivable; decentralization is the cure.

## Change 2: RandomX memory fix

To be precise up front: this is not a runaway memory leak, as some suggested. RAM does not grow without bound; it steps up once and holds.

Since HF22, many nodes' RAM climbs to roughly three times its baseline after a while and stays there, pushing busy servers into swap. The cause connects directly to the point above. A node loads a 256 MB RandomX cache the first time it verifies a PoW fallback block, and the software never releases it.

HF22 is when PoW fallback blocks became a regular occurrence, because the fallback keeps the chain alive through stalls, so nodes began carrying that cache permanently. The more stalls, the more nodes held it. RandomX itself did not change: HF22 simply made nodes start verifying PoW fallback blocks, which is what loads and pins the cache.

This release frees the cache once it goes idle, and because Pulse-recovery makes fallback blocks PoW-free, nodes stop verifying PoW altogether and no longer need it. Measured on testnet: **about 80 percent per-node RAM reduction**, held steady through stall cycles.

## For node operators

- **Upgrade before the activation height.** This changes which blocks are valid (accepting the PoW-free fallback), so a node that is not on the new binary by the activation block will reject fallback blocks and fork off the network. The height ships with the binaries.
- **Restart after upgrading.** A restart clears any already-held cache so you see the lower footprint immediately.
- **Expect lower, stable RAM per node.**
- **Spread your nodes out.** If you run many nodes, distribute them across independent, adequately-provisioned hosts and providers. Concentrated fleets are the main source of the stalls this release resolves.

## On network health: what is not changing

The memory fix corrects an accidental cost, a cache that was never freed, which affected all operators without distinction. It is not a relaxation of our standards, and not a softening of our commitment to resilience and decentralization.

Future releases will add capacity and health enforcement. Nodes that are under-provisioned or over-concentrated on a few machines will be flagged and, where appropriate, decommissioned until corrected, alongside measures that favor nodes spread across diverse, independent infrastructure. Operators running many nodes on a small number of servers should start distributing now.

---

*XEQM Core, hf22_sn_policy service-node revision 1. Validated on a multi-host testnet: 3/3 stall and recovery cycles, approx. 80% per-node RAM reduction.*
