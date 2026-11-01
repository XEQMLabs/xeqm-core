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

A shared-chain host runs one writer and N readers against one chain in one data directory.

- The writer is a keyless chain service. It is an ordinary daemon that syncs the chain over P2P and
  commits blocks, with no service node keys and no quorum, Pulse or uptime-proof duties. Block
  production is not the writer's job: Pulse blocks are produced and signed by the quorum and reach
  the writer through normal block handling, which the writer commits like any syncing node. Keeping
  the writer keyless means a writer crash never also drops a service node slot.
- The readers are the host's service nodes, each a daemon started with `--db-readonly`. They hold
  their own keys and proofs, read the shared chain, and participate in quorum and Pulse duties from
  read-only chain state (Phase 1).

One chain copy on disk, shared through the OS page cache because LMDB is memory-mapped, serves all
of them. This is the per-host memory and disk saving the feature exists for.

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

Re-exec forces one genuine decision, because the promoting node is a service node (it holds keys)
while the steady-state writer is meant to be keyless. Two coherent shapes:

- A dedicated keyless writer process per host, with the reader service nodes separate. When the
  writer dies the readers race for the lock and the winner re-execs into a keyless writer, which
  costs that node its service node slot until an operator rebalances. This keeps the writer keyless
  but makes a failover also a service-node outage on that host.
- Every node on the host is a service node, and whichever one holds the lock additionally opens the
  chain read-write and does the writer's work. Promotion is then just a re-exec that drops
  `--db-readonly` while keeping the node's identity, and no service node slot is lost. The writer is
  not keyless, but there is no separate writer process to lose independently, so the crash-isolation
  that keyless-ness buys is moot here.

The second shape is the simpler and more faithful reading of "auto-promote a reader", and it is the
recommendation, with the keyless property kept as the steady-state ideal (a dedicated keyless writer
can still be the normal writer, supervised to restart, with reader promotion as the backup when it
cannot). This is the open decision called out below.

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
2. The writer: a normal daemon that takes the lock at startup, runs the lease and watchdog, and
   refuses to write (falls back to reader, or exits, per the startup policy) if it cannot take it.
3. Reader promotion: the monitor plus the re-exec helper that restarts the daemon in writer mode with
   the lock descriptor held across the `execv`.
4. A single-host soak on the testnet: one writer, several readers, kill and stall the writer, and
   confirm exactly-one-writer holds across clean exit, crash, and stall, with bounded failover time
   and no two-writer window.
5. Fleet rollout, missoula first.

## Open questions

- Writer identity (the main one, see the promotion section): a dedicated keyless writer whose
  failover costs a service node slot, or every node a service node with the lock-holder also writing.
  The recommendation is the second, keeping keyless as the steady-state ideal with reader promotion as
  the backup.
- Startup policy when the lock is already held: should a daemon started as a writer but finding the
  lock taken exit, or fall back to running as a reader. Falling back is friendlier for a host where
  the operator starts several daemons without designating the writer.
- Whether a dedicated keyless writer, if run, is a trimmed build or the full daemon with service node
  duties disabled.
- Exact values for `T_renew`, `T_fence`, the reader staleness threshold and `T_poll`, set during the
  soak.
