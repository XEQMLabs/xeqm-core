# Shared per-host chain storage, Phase 2: the keyless writer and reader auto-promotion

Tracking issue: #62. Follows Phase 1 (`docs/shared-per-host-chain-phase-1.md`), which built the
read-only reader: a daemon can open the shared chain with `--db-readonly`, keep its service node
list, ONS and mempool views current, and never write the shared database.

Internal R&D scope, LMDB first, as decided in the release plan. This document designs the other
half of the shared-chain model: the single writer that maintains the chain, and the failover that
promotes a reader to writer when the writer is lost. The failover model was decided on 2026-10-31:
auto-promote a reader, with leadership anchored to the write lock, a renewing lease for the
alive-but-stalled writer, and mandatory writer self-fencing, no preemption of a healthy writer, and
the leadership token kept local to the host.

## Roles

A shared-chain host runs one writer and N readers against one chain in one data directory. The role,
writer or reader, is independent of what kind of node the daemon is: a service node, a public RPC
node, and a seed are all the same daemon opening the same chain, and any of them can be the writer or
a reader. A host often runs a mix, for example a public node or seed alongside several service nodes.

- The writer is whichever daemon holds the write lock. It syncs the chain over P2P and commits blocks
  read-write. Block production is not the writer's job: Pulse blocks are produced and signed by the
  quorum and reach the writer through normal block handling, which the writer commits like any
  syncing node, so the writer needs no service node keys.
- The readers are every other daemon on the host, each started with `--db-readonly`. A reader that is
  a service node holds its own keys and proofs and does quorum and Pulse duties from read-only chain
  state (Phase 1); a reader that is a public node or seed just serves and syncs from the shared chain.

Keyless-ness is a property of the writer worth preserving, because a keyless writer crash never also
drops a service node slot. It falls out naturally on a mixed host: a public node or seed is already
keyless and already full-syncs the chain, so it is the natural steady-state writer, with the service
nodes as readers. Reader promotion then only has to cover the case where that keyless writer is lost.
On a host that runs only service nodes there is no keyless daemon to prefer, so one service node holds
the lock and writes; it is not literally keyless, but there is no separate writer process to lose
independently, so the crash-isolation that keyless-ness buys does not apply there.

One chain copy on disk, shared through the OS page cache because LMDB is memory-mapped, serves all of
them. This is the per-host memory and disk saving the feature exists for.

## Why not simply let any node write

LMDB is single-writer: one write transaction at a time across the whole environment, enforced by a
mutex in `lock.mdb`. Two daemons both trying to maintain the chain would serialise on that mutex and,
worse, both run the derived-state writes (service node list, ONS, reward batching, mempool) that each
assume sole ownership. The invariant the whole model rests on is exactly one writer at a time. The
failover design exists to preserve that invariant across a writer failure without ever allowing two.

## The failure the design must survive

Three cases, in increasing difficulty:

1. The writer exits cleanly. It releases everything; a reader should take over promptly.
2. The writer crashes. Same as a clean exit from the point of view of the host: the kernel releases
   the process's locks immediately.
3. The writer is alive but stalled (a long garbage-collection pause, IO stall, or partial hang). It
   still holds its locks and could, in principle, wake up and resume writing. This is the dangerous
   case, because a naive "the writer looks dead, I'll take over" rule here produces two writers. The
   forbidden state is two writers, not a brief gap with none.

## Leadership is the write lock, not a vote

A reader never promotes because it judged the writer dead. It promotes only by acquiring an exclusive
host-local lock. Whoever holds the lock is the writer by definition, so there is no election and no
split-brain from disagreement.

The lock is an advisory exclusive `flock` on a host-local file, `writer.lock` in the chain data
directory (host-local by construction, never network-exposed). The writer takes `LOCK_EX` at startup
and holds it for its whole life. The kernel releases an `flock` the instant the holding process dies,
cleanly or by crash, so cases 1 and 2 need no timeout at all: a reader's non-blocking `LOCK_EX`
attempt fails while a live writer holds it and succeeds the moment the writer is gone.

`flock` alone does not cover case 3, because a stalled-but-alive writer still holds it. That is what
the lease and self-fence are for.

## The lease and self-fencing turn a stall into a death

The writer maintains a lease: a small record in the lock file carrying its instance identity and a
timestamp it rewrites every `T_renew`. The lease has two jobs.

- Self-fence (the safety property). The writer runs a watchdog. If it cannot confirm forward progress
  and a fresh lease for longer than `T_fence`, it stops writing immediately and exits the process. A
  writer that stalls past `T_fence` therefore dies rather than lingering, which releases its `flock`
  and its LMDB mutex and converts the hard case 3 into the easy case 2. Self-fencing by exit also
  sidesteps the LMDB write mutex: a process stalled while holding the write transaction would block a
  new writer indefinitely, so the stalled writer must leave rather than be worked around. A writer
  that wakes after being fenced finds it no longer owns the lease and does not write; it comes back,
  if at all, as a reader.
- Liveness signal and observability (the convenience property). Readers watch the lease timestamp to
  know when to begin trying to acquire the lock, and operators read it to see which instance is the
  writer and how fresh its lease is. The lease is advisory for readers; the `flock` acquisition, not
  the timestamp, is the authority that admits a new writer.

`T_fence` is strictly less than the lease staleness a reader waits for, so the writer always fences
itself before any reader concludes the lease is dead. That ordering is what guarantees the old writer
has stopped before a new one starts.

## Promotion: a reader becomes the writer

A reader runs a promotion monitor. On an interval `T_poll` it attempts a non-blocking `LOCK_EX` on
`writer.lock`. While a healthy writer holds the lock the attempt fails and the reader stays a reader,
so a healthy writer is never preempted. When the attempt succeeds (the writer exited or self-fenced
and died), the reader has leadership and promotes.

Promotion cannot flip the open database from read-only to read-write in place. LMDB fixes the mode
when the environment is opened and offers no way to change it, and `BlockchainLMDB::open` refuses to
reopen an already-open handle, so switching modes means destroying and rebuilding `Blockchain::m_db`.
Worse, read-only-ness is consumed once at startup in work the reader deliberately skipped: the writer
catch-up and block-pop loop and `fixup()` in `Blockchain::init`, checkpoint loading and pruning in
`core::init`, and opening ONS in read-only mode. `Blockchain::init` is written to run exactly once,
and the only teardown, `deinit()`, closes the database and drops the pointer rather than offering a
re-init. An in-process handle swap is therefore not realistic.

So promotion re-execs the daemon into writer mode. The reader acquires the lock on a file descriptor
it keeps open (not close-on-exec), then `execv`s its own binary with the writer arguments (dropping
`--db-readonly`). The new image inherits the still-held lock descriptor, so leadership is continuous
across the restart with no window for a second acquirer, and normal startup then runs the writer
catch-up, fixup and checkpoint load that a reader skips. The abrupt replacement is safe precisely
because a read-only reader has nothing to flush: its database handle is read-only, so there is no
dirty state lost by not running a clean shutdown. The daemon has no self-restart today, so this adds
a small re-exec helper; the lock reuses `tools::file_locker` (`src/common/file.h`), currently
wallet-only, which already wraps `flock`.

Failover time is therefore one daemon restart plus a warm chain load, on the order of seconds, and
bounded because the promoting reader was already at the tip. The lock is held continuously from
acquisition through the re-exec, so there is never a gap in which a second reader could also acquire
it.

The promoting node may be a service node, which holds keys, while the steady-state writer is ideally
keyless. The role model in Roles resolves this without a separate writer process. On a host that runs
a public node or seed, that keyless daemon is the steady-state writer and the service nodes are
readers; if it is lost, a service node reader re-execs into writer mode and carries the chain until
the keyless daemon returns and reclaims the role, so the key-holding writer is only a temporary
failover state, not the norm. On a host with only service nodes, whichever holds the lock writes
while keeping its identity, and no service node slot is lost to promotion. Either way promotion is the
same re-exec that drops `--db-readonly`; the only difference is which daemon prefers to hold the lock
in steady state (see the lock-preference open question below).

## No preemption, and the recovered writer

A reader never challenges a healthy writer: the only path to becoming a writer is acquiring a lock the
healthy writer holds, which cannot happen. A former writer that recovers after being fenced or after a
restart finds the lock held by the new writer, fails to acquire it, and runs as a reader. There is no
hand-back and no demotion of the healthy incumbent.

## Timeouts and the tuning trade-off

Four intervals: `T_renew` (how often the writer rewrites its lease), `T_fence` (how long a writer
tolerates no progress before exiting), the reader's lease-staleness threshold, and `T_poll` (how
often a reader tries the lock). The ordering constraint is `T_renew` < `T_fence` < reader staleness
threshold, so the writer fences itself before readers act on staleness, and `T_poll` sets how quickly
a reader notices a freed lock.

The trade-off is the usual one. Too short and a writer under transient load fences itself and the
host churns its writer (a flap); every reader on the host then briefly has a stale tip together, and
flapping is itself a symptom of a sick box. Too long and the host runs without a writer for that long
after a real failure, during which the readers' chain tip stops advancing. These values are tuned
during the fleet soak, not fixed here, and the design treats frequent failovers as an alarm, not a
normal event.

## Correlated failure stays covered

One chain and one writer per host is a shared failure domain. The standing requirement is unchanged
and independent of this design: at least one node keeps an independent current LMDB chain and the
LMDB bootstrap keeps publishing, so a host whose writer and readers all stall together still has a
fallback that needs no code patch.

## Observability

The writer's identity and lease freshness are readable from the lock file. The daemon exposes which
role it is running and, for a writer, its lease age; a failover emits a log event and increments a
counter; repeated failovers in a short window raise an alarm, because a healthy host fails over rarely
if ever.

## Phased plan

1. The lock and lease module: acquire, renew, read, and the self-fence watchdog, with the file format
   and the host-local path, built on `tools::file_locker` (`src/common/file.h`). Standalone and
   unit-testable without the daemon.
2. The writer. Done: `--shared-chain-writer` takes the lock before opening the chain read-write,
   renews the lease and heartbeats the self-fence watchdog from the idle loop (so a stalled loop
   fences the writer and exits), refuses to start if the lock is held, and releases the lock on clean
   shutdown after the final writes. Validated on the testnet across a second-writer refusal, a reader
   attaching alongside, and the lock freeing on both clean shutdown and crash.
3. Reader promotion: the monitor plus the re-exec helper that restarts the daemon in writer mode with
   the lock descriptor held across the `execv`.
4. A single-host soak on the testnet: one writer, several readers, kill and stall the writer, and
   confirm exactly-one-writer holds across clean exit, crash, and stall, with bounded failover time
   and no two-writer window.
5. Fleet rollout, missoula first.

## Open questions

- Lock preference. On a mixed host the keyless public node or seed should hold the lock in steady
  state and the service nodes should defer to it, promoting only if it is lost. This needs a
  preference, so a service node does not win the lock ahead of the keyless daemon on a normal start.
  Options: the keyless daemon takes the lock eagerly while service nodes wait a short grace period
  before trying; or an explicit per-node preference flag. To be decided at implementation.
- Startup policy when the lock is already held. Implemented for now as refuse-and-exit: a daemon
  started with `--shared-chain-writer` that cannot take the lock logs that another writer holds it and
  fails to start, telling the operator to run it with `--db-readonly` instead. Falling back to reader
  is friendlier but only useful once reader promotion exists (step 3), so a fallen-back daemon can
  actively promote rather than sit as a permanent reader; revisit then.
- Exact values for `T_renew`, `T_fence`, the reader staleness threshold and `T_poll`, set during the
  soak.
