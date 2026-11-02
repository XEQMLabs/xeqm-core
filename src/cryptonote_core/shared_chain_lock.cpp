#include "shared_chain_lock.h"

#include <array>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <random>

#include <fmt/std.h>

#include "logging/oxen_logger.h"

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace cryptonote::shared_chain {

static auto logcat = oxen::log::Cat("sharedchain");

namespace {
    // Lease record wire format: a single short line. "XWL1" magic, then the instance id (16 hex), the
    // writer pid, and the last-renew wall-clock seconds. A reader that cannot parse it treats the
    // lease as absent, which also guards against a torn read of a partial write.
    constexpr std::string_view LEASE_MAGIC = "XWL1";
    constexpr size_t LEASE_MAX = 128;

    int64_t now_unix() {
        return std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                .count();
    }

    uint64_t make_instance_id() {
        std::random_device rd;
        return (static_cast<uint64_t>(rd()) << 32) ^ static_cast<uint64_t>(rd()) ^
               static_cast<uint64_t>(now_unix());
    }
}  // namespace

writer_lock::writer_lock(fs::path lock_file) :
        m_file{std::move(lock_file)}, m_instance_id{make_instance_id()} {}

writer_lock::~writer_lock() {
    release();
}

#ifndef _WIN32

bool writer_lock::try_acquire() {
    if (m_held)
        return true;

    // Deliberately no O_CLOEXEC: a promotion must be able to carry this fd (and so the lock) across
    // an execv into writer mode without a gap.
    int fd = ::open(m_file.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd == -1) {
        oxen::log::warning(
                logcat, "shared-chain writer lock: cannot open {}: {}", m_file, std::strerror(errno));
        return false;
    }

    struct flock fl {};
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;  // whole file
    if (::fcntl(fd, F_SETLK, &fl) == -1) {
        if (errno == EACCES || errno == EAGAIN)
            // Another process holds the writer lock; this process stays a reader.
            oxen::log::debug(logcat, "shared-chain writer lock already held by another process");
        else
            oxen::log::warning(
                    logcat,
                    "shared-chain writer lock: fcntl lock on {} failed: {}",
                    m_file,
                    std::strerror(errno));
        ::close(fd);
        return false;
    }

    m_fd = fd;
    m_held = true;
    write_record();
    oxen::log::info(
            logcat,
            "shared-chain writer lock acquired on {} (instance {:016x})",
            m_file,
            m_instance_id);
    return true;
}

void writer_lock::write_record() {
    if (m_fd == -1)
        return;
    std::array<char, LEASE_MAX> buf{};
    int n = std::snprintf(
            buf.data(),
            buf.size(),
            "%.*s %016" PRIx64 " %" PRId64 " %" PRId64 "\n",
            static_cast<int>(LEASE_MAGIC.size()),
            LEASE_MAGIC.data(),
            m_instance_id,
            static_cast<int64_t>(::getpid()),
            now_unix());
    if (n <= 0)
        return;
    // One pwrite at offset 0 plus a truncate: the record is small enough that a reader's single read
    // sees it whole, and the truncate removes any longer previous record.
    if (::pwrite(m_fd, buf.data(), static_cast<size_t>(n), 0) != n)
        oxen::log::warning(logcat, "shared-chain writer lease: pwrite failed: {}", std::strerror(errno));
    else if (::ftruncate(m_fd, n) == -1)
        oxen::log::warning(
                logcat, "shared-chain writer lease: ftruncate failed: {}", std::strerror(errno));
}

void writer_lock::renew() {
    if (m_held)
        write_record();
}

void writer_lock::release() {
    if (m_fd != -1) {
        ::close(m_fd);  // closing the fd drops the fcntl lock
        m_fd = -1;
    }
    m_held = false;
}

std::optional<lease_info> writer_lock::read(const fs::path& lock_file) {
    int fd = ::open(lock_file.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd == -1)
        return std::nullopt;
    std::array<char, LEASE_MAX> buf{};
    ssize_t n = ::pread(fd, buf.data(), buf.size() - 1, 0);
    ::close(fd);
    if (n <= 0)
        return std::nullopt;
    buf[static_cast<size_t>(n)] = '\0';

    char magic[8] = {};
    lease_info info{};
    // Expect: "<magic> <instance:hex> <pid> <renew>"
    if (std::sscanf(
                buf.data(),
                "%7s %" SCNx64 " %" SCNd64 " %" SCNd64,
                magic,
                &info.instance_id,
                &info.pid,
                &info.renew_unix) != 4)
        return std::nullopt;
    if (LEASE_MAGIC != magic)
        return std::nullopt;
    return info;
}

std::unique_ptr<writer_lock> writer_lock::adopt(fs::path lock_file, int fd) {
    if (fd < 0) {
        oxen::log::error(logcat, "shared-chain writer lock adopt: invalid fd {}", fd);
        return nullptr;
    }
    // The fd must be a valid, open descriptor inherited across the execv. If it is not, the
    // promotion lost the lock and must not proceed as a writer.
    if (::fcntl(fd, F_GETFD) == -1) {
        oxen::log::error(
                logcat,
                "shared-chain writer lock adopt: fd {} is not open ({}); lock not inherited",
                fd,
                std::strerror(errno));
        return nullptr;
    }
    // Re-assert the write lock on the inherited fd. We already hold it (fcntl locks survive execve),
    // so this succeeds for us and is a no-op; a failure means we do not actually hold it.
    struct flock fl {};
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    if (::fcntl(fd, F_SETLK, &fl) == -1) {
        oxen::log::error(
                logcat,
                "shared-chain writer lock adopt: cannot confirm lock on fd {}: {}",
                fd,
                std::strerror(errno));
        return nullptr;
    }

    std::unique_ptr<writer_lock> lock{new writer_lock{std::move(lock_file)}};
    lock->m_fd = fd;  // take ownership of the inherited descriptor as-is (do not reopen)
    lock->m_held = true;
    lock->write_record();  // stamp a fresh lease for this new writer process (new instance id)
    oxen::log::info(
            logcat,
            "shared-chain writer lock adopted on {} across promotion (instance {:016x}, fd {})",
            lock->m_file,
            lock->m_instance_id,
            fd);
    return lock;
}

#else  // _WIN32: the shared per-host chain is a Linux-fleet feature; stub so the daemon still builds.

bool writer_lock::try_acquire() {
    oxen::log::warning(logcat, "shared-chain writer lock is not supported on Windows");
    return false;
}
void writer_lock::write_record() {}
void writer_lock::renew() {}
void writer_lock::release() {
    m_fd = -1;
    m_held = false;
}
std::optional<lease_info> writer_lock::read(const fs::path&) {
    return std::nullopt;
}
std::unique_ptr<writer_lock> writer_lock::adopt(fs::path, int) {
    return nullptr;
}

#endif

// ---- fence_watchdog ---------------------------------------------------------------------------

fence_watchdog::fence_watchdog(
        std::chrono::milliseconds fence_timeout,
        std::chrono::milliseconds check_interval,
        std::function<void()> on_fence) :
        m_timeout{fence_timeout},
        m_interval{check_interval},
        m_on_fence{std::move(on_fence)} {}

fence_watchdog::~fence_watchdog() {
    stop();
}

int64_t fence_watchdog::now_ms() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
}

void fence_watchdog::start() {
    if (m_run.exchange(true))
        return;  // already running
    m_last_beat_ms.store(now_ms(), std::memory_order_relaxed);
    m_fenced.store(false, std::memory_order_relaxed);
    m_thread = std::thread([this] { loop(); });
}

void fence_watchdog::heartbeat() {
    m_last_beat_ms.store(now_ms(), std::memory_order_relaxed);
}

void fence_watchdog::stop() {
    if (!m_run.exchange(false))
        return;
    if (m_thread.joinable())
        m_thread.join();
}

void fence_watchdog::loop() {
    const int64_t timeout_ms = m_timeout.count();
    while (m_run.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(m_interval);
        if (!m_run.load(std::memory_order_relaxed))
            break;
        int64_t idle = now_ms() - m_last_beat_ms.load(std::memory_order_relaxed);
        if (idle > timeout_ms) {
            // The writer has not made progress within the fence timeout: it is stalled. Fire once.
            if (!m_fenced.exchange(true)) {
                oxen::log::error(
                        logcat,
                        "shared-chain writer self-fence: no progress for {} ms (limit {} ms); "
                        "stopping writes",
                        idle,
                        timeout_ms);
                if (m_on_fence)
                    m_on_fence();
            }
            break;  // after firing, stop checking (on_fence is expected to exit the process)
        }
    }
}

}  // namespace cryptonote::shared_chain
