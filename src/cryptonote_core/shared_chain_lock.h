#pragma once

// Shared per-host chain storage (issue #62), Phase 2: the writer lock, lease, and self-fence
// watchdog. See docs/shared-per-host-chain-phase-2.md.
//
// Exactly one daemon on a host maintains the shared chain read-write (the writer); the rest open it
// read-only (Phase 1). Leadership is a host-local exclusive lock: whoever holds it is the writer, so
// there is no election. The kernel releases the lock when the holding process dies (cleanly or by
// crash), so a dead writer frees it with no timeout. A renewing lease plus a self-fence watchdog
// cover the alive-but-stalled writer by turning a stall into a clean exit.
//
// This module is standalone (no chain dependencies) and unit-testable on its own.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <thread>

namespace cryptonote::shared_chain {

namespace fs = std::filesystem;

// Writer role, recorded in the lease so another process can tell a designated writer from a failover
// one. A designated (primary) writer is one started with --shared-chain-writer and no inherited lock
// (the dedicated per-host writer, e.g. the watcher); a promoted (secondary) writer is a reader that
// took leadership over via failover. The distinction lets a returning designated writer reclaim the
// lock from a promoted one instead of failing to start. 'unknown' covers an old-format lease.
inline constexpr char WRITER_ROLE_PRIMARY = 'P';   // designated writer (no inherited lock fd)
inline constexpr char WRITER_ROLE_PROMOTED = 'S';  // took over via reader promotion (failover)
inline constexpr char WRITER_ROLE_UNKNOWN = '?';   // lease had no role field (older writer)

// The lease record a reader observes and the writer rewrites on renew. Host-local: the renew time is
// wall-clock seconds compared only against the same host's clock.
struct lease_info {
    uint64_t instance_id = 0;  // random, unique per writer process
    int64_t pid = 0;           // the writer's pid (for operators; not used for decisions)
    int64_t renew_unix = 0;    // wall-clock seconds of the last renew
    char role = WRITER_ROLE_UNKNOWN;  // 'P' designated, 'S' promoted, '?' old-format lease

    // Seconds since the last renew, given the current wall-clock time.
    int64_t age(int64_t now_unix) const { return now_unix - renew_unix; }
};

// A reclaim request: a returning designated writer that finds the lock held by a PROMOTED writer
// drops this marker (file "<chain-dir>/writer.reclaim") to ask the promoted writer to step down and
// hand leadership back, rather than failing because the lock is held. The requester refreshes the
// timestamp while it waits; a promoted writer, and a promotable reader, honour the request only while
// it is fresh and the requester's pid is alive, so a requester that dies mid-handoff stops
// suppressing failover within a few seconds. Contents: "XRQ1 <pid> <unix_ts>\n".
struct reclaim_request {
    int64_t pid = 0;
    int64_t ts_unix = 0;
};
// Seconds a reclaim request stays honoured since its last refresh.
inline constexpr int64_t RECLAIM_REQUEST_FRESH_SECS = 5;
// Write (or refresh) the reclaim request in chain_dir; pid is the requesting designated writer's pid.
void write_reclaim_request(const fs::path& chain_dir, int64_t pid);
// Read the reclaim request if present and parseable; nullopt otherwise.
std::optional<reclaim_request> read_reclaim_request(const fs::path& chain_dir);
// Remove the reclaim request (the requester clears it once the handoff completes or times out).
void clear_reclaim_request(const fs::path& chain_dir);
// True if the request is still fresh (recent timestamp) and its pid is alive.
bool reclaim_request_live(const reclaim_request& req);

// Writer-side exclusive lock on a host-local lock file, taken with a non-blocking fcntl write lock
// (the same primitive tools::file_locker uses). The kernel drops the lock on process death, so clean
// exit and crash both free it immediately. The same file holds the lease record. The lock fd is kept
// open and deliberately NOT close-on-exec, so a later promotion can carry leadership across an execv
// with no gap.
//
// WARNING (fcntl semantics): an fcntl record lock is dropped if the owning process closes ANY file
// descriptor referring to the lock file. A process that holds the lock must therefore not also open
// the lock file to read() it; it uses its own in-memory lease state instead. The static read() below
// is for OTHER processes (readers observing the writer).
class writer_lock {
  public:
    // role is recorded in the lease (WRITER_ROLE_PRIMARY for a designated writer, WRITER_ROLE_PROMOTED
    // for one taking over via failover) so another process can tell them apart.
    explicit writer_lock(fs::path lock_file, char role = WRITER_ROLE_PRIMARY);
    ~writer_lock();
    writer_lock(const writer_lock&) = delete;
    writer_lock& operator=(const writer_lock&) = delete;

    // Try to take the exclusive lock without blocking. Returns true if this process holds it after
    // the call (including if it already held it). On a fresh acquisition it writes the initial lease.
    bool try_acquire();

    bool held() const { return m_held; }

    // Rewrite the lease with a fresh timestamp. The writer calls this periodically. No-op if not
    // held.
    void renew();

    // Release the lock and close the fd.
    void release();

    uint64_t instance_id() const { return m_instance_id; }

    // The underlying lock fd, or -1. Exposed so a promotion can clear close-on-exec and inherit it
    // across execv (Phase 2 step 3). Do not close it elsewhere (see the fcntl warning above).
    int fd() const { return m_fd; }

    const fs::path& path() const { return m_file; }

    // Reader-side: read the current lease without taking any lock (the lock is advisory, so this
    // works while the writer holds it). Returns nullopt if the file is absent, empty, or unparseable.
    // Must not be called by a process that holds the lock (see the fcntl warning above).
    static std::optional<lease_info> read(const fs::path& lock_file);

    // Promotion: adopt a lock fd inherited across an execv. The calling process must already hold
    // the fcntl write lock on `fd` (the pre-exec reader acquired it; fcntl record locks survive
    // execve). This does NOT reopen the file, because opening and later closing a second fd would
    // drop the inherited lock (see the fcntl warning above); it takes ownership of `fd` as-is. It
    // re-asserts the lock as a sanity check and writes a fresh lease for the new writer process.
    // Returns nullptr if `fd` is not a valid, held lock. On success the returned lock owns `fd` and
    // releases it on destruction, exactly like an acquired lock. A promotion always adopts as a
    // promoted (failover) writer, so role defaults to WRITER_ROLE_PROMOTED.
    static std::unique_ptr<writer_lock> adopt(
            fs::path lock_file, int fd, char role = WRITER_ROLE_PROMOTED);

  private:
    void write_record();  // serialise the lease to the held fd at offset 0

    fs::path m_file;
    int m_fd = -1;
    uint64_t m_instance_id = 0;
    char m_role = WRITER_ROLE_PRIMARY;
    bool m_held = false;
};

// Self-fence watchdog. The writer calls heartbeat() whenever it makes forward progress (a renew, a
// committed block, a healthy main-loop pass). A dedicated thread, which is not blocked by a stalled
// main loop, checks the time since the last heartbeat; if it exceeds the fence timeout the writer has
// stalled, so the watchdog invokes on_fence exactly once. on_fence is expected to stop writing and
// exit the process hard, which releases the lock and lets a reader promote. Uses a monotonic clock so
// a wall-clock jump never trips or masks it.
class fence_watchdog {
  public:
    fence_watchdog(
            std::chrono::milliseconds fence_timeout,
            std::chrono::milliseconds check_interval,
            std::function<void()> on_fence);
    ~fence_watchdog();
    fence_watchdog(const fence_watchdog&) = delete;
    fence_watchdog& operator=(const fence_watchdog&) = delete;

    void start();      // begins the watchdog thread; records an initial heartbeat
    void heartbeat();  // thread-safe; records "progress happened now"
    void stop();       // stops the thread (does not fence)

    bool fenced() const { return m_fenced.load(std::memory_order_relaxed); }

  private:
    void loop();
    int64_t now_ms() const;

    std::chrono::milliseconds m_timeout;
    std::chrono::milliseconds m_interval;
    std::function<void()> m_on_fence;
    std::atomic<int64_t> m_last_beat_ms{0};
    // No self-fence until the first heartbeat: a bootstrap-started writer may spend minutes in the
    // one-time service-node-list rescan before reaching the idle loop; that is startup, not a stall.
    std::atomic<bool> m_beaten{false};
    std::atomic<bool> m_run{false};
    std::atomic<bool> m_fenced{false};
    std::thread m_thread;
};

}  // namespace cryptonote::shared_chain
