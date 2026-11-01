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

// The lease record a reader observes and the writer rewrites on renew. Host-local: the renew time is
// wall-clock seconds compared only against the same host's clock.
struct lease_info {
    uint64_t instance_id = 0;  // random, unique per writer process
    int64_t pid = 0;           // the writer's pid (for operators; not used for decisions)
    int64_t renew_unix = 0;    // wall-clock seconds of the last renew

    // Seconds since the last renew, given the current wall-clock time.
    int64_t age(int64_t now_unix) const { return now_unix - renew_unix; }
};

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
    explicit writer_lock(fs::path lock_file);
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
    // releases it on destruction, exactly like an acquired lock.
    static std::unique_ptr<writer_lock> adopt(fs::path lock_file, int fd);

  private:
    void write_record();  // serialise the lease to the held fd at offset 0

    fs::path m_file;
    int m_fd = -1;
    uint64_t m_instance_id = 0;
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
    std::atomic<bool> m_run{false};
    std::atomic<bool> m_fenced{false};
    std::thread m_thread;
};

}  // namespace cryptonote::shared_chain
