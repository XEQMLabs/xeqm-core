// Tests for the shared per-host chain writer lock, lease, and self-fence watchdog
// (src/cryptonote_core/shared_chain_lock.*). See docs/shared-per-host-chain-phase-2.md.

#include "gtest/gtest.h"

#include "cryptonote_core/shared_chain_lock.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace std::chrono_literals;
namespace sc = cryptonote::shared_chain;
namespace fs = std::filesystem;

namespace {
fs::path temp_lock_path(const char* tag) {
    auto p = fs::temp_directory_path() /
             ("xeqm-wl-" + std::string{tag} + "-" + std::to_string(::getpid()) + "-" +
              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".lock");
    std::error_code ec;
    fs::remove(p, ec);
    return p;
}
}  // namespace

#ifndef _WIN32

// Acquire / renew / release lifecycle, and that the lease is readable (read() done after release,
// since a lock-holder must not open the file itself).
TEST(shared_chain_lock, lifecycle_and_lease) {
    auto path = temp_lock_path("life");

    sc::writer_lock lock{path};
    EXPECT_FALSE(lock.held());
    ASSERT_TRUE(lock.try_acquire());
    EXPECT_TRUE(lock.held());
    EXPECT_TRUE(lock.try_acquire());  // idempotent while held
    EXPECT_NE(lock.fd(), -1);
    EXPECT_NE(lock.instance_id(), 0u);

    lock.renew();

    // read() after release (the holder must not read its own lock file).
    uint64_t id = lock.instance_id();
    lock.release();
    EXPECT_FALSE(lock.held());

    auto info = sc::writer_lock::read(path);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->instance_id, id);
    EXPECT_EQ(info->pid, static_cast<int64_t>(::getpid()));
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
    EXPECT_LE(info->age(now), 5);  // renewed moments ago

    std::error_code ec;
    fs::remove(path, ec);
}

// read() of an absent or garbage file returns nullopt (also the torn-read guard).
TEST(shared_chain_lock, read_absent_and_garbage) {
    auto path = temp_lock_path("absent");
    EXPECT_FALSE(sc::writer_lock::read(path).has_value());

    {
        std::ofstream f{path};
        f << "not a valid lease record\n";
    }
    EXPECT_FALSE(sc::writer_lock::read(path).has_value());
    std::error_code ec;
    fs::remove(path, ec);
}

// Cross-process exclusivity: while this process holds the lock, a forked child cannot acquire it;
// after release, a child can.
TEST(shared_chain_lock, cross_process_exclusion) {
    auto path = temp_lock_path("excl");

    auto child_tries_acquire = [&path]() -> bool {
        pid_t pid = ::fork();
        if (pid == 0) {
            // Child: a fresh lock object, its own fd. _exit(0) if it acquired, _exit(1) if not.
            sc::writer_lock child_lock{path};
            bool got = child_lock.try_acquire();
            _exit(got ? 0 : 1);
        }
        int status = 0;
        ::waitpid(pid, &status, 0);
        EXPECT_TRUE(WIFEXITED(status));
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    };

    sc::writer_lock lock{path};
    ASSERT_TRUE(lock.try_acquire());

    // Parent holds it: the child must fail to acquire.
    EXPECT_FALSE(child_tries_acquire());

    // Parent releases: now a child can acquire (and releases on _exit).
    lock.release();
    EXPECT_TRUE(child_tries_acquire());

    std::error_code ec;
    fs::remove(path, ec);
}

// adopt() takes ownership of an already-held lock fd (the promotion path, where the fd is inherited
// across execv). Here we stand in for the pre-exec acquire by locking an fd ourselves, then adopt
// it: the adopted lock reports held, carries a fresh instance id, and writes a readable lease.
TEST(shared_chain_lock, adopt_inherited_fd) {
    auto path = temp_lock_path("adopt");

    // Stand in for the pre-exec reader: open without O_CLOEXEC and take the write lock.
    int fd = ::open(path.c_str(), O_RDWR | O_CREAT, 0644);
    ASSERT_NE(fd, -1);
    struct flock fl {};
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    ASSERT_NE(::fcntl(fd, F_SETLK, &fl), -1);

    auto lock = sc::writer_lock::adopt(path, fd);
    ASSERT_TRUE(lock);  // adopt took ownership of fd
    EXPECT_TRUE(lock->held());
    EXPECT_EQ(lock->fd(), fd);
    EXPECT_NE(lock->instance_id(), 0u);
    uint64_t id = lock->instance_id();

    // Another process must not be able to acquire while we (via the adopted fd) hold it.
    auto child_can_acquire = [&path]() -> bool {
        pid_t pid = ::fork();
        if (pid == 0) {
            sc::writer_lock child{path};
            bool got = child.try_acquire();
            _exit(got ? 0 : 1);
        }
        int status = 0;
        ::waitpid(pid, &status, 0);
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    };
    EXPECT_FALSE(child_can_acquire());

    // The adopted lock rewrote the lease with the new instance id; release first, then read.
    lock->release();
    EXPECT_FALSE(lock->held());
    auto info = sc::writer_lock::read(path);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->instance_id, id);
    EXPECT_EQ(info->pid, static_cast<int64_t>(::getpid()));

    std::error_code ec;
    fs::remove(path, ec);
}

// adopt() rejects a descriptor it cannot use: a negative fd, or one that is not open.
TEST(shared_chain_lock, adopt_rejects_invalid_fd) {
    auto path = temp_lock_path("adopt-bad");
    EXPECT_FALSE(sc::writer_lock::adopt(path, -1));

    int fd = ::open(path.c_str(), O_RDWR | O_CREAT, 0644);
    ASSERT_NE(fd, -1);
    ::close(fd);  // now closed: adopt must refuse it
    EXPECT_FALSE(sc::writer_lock::adopt(path, fd));

    std::error_code ec;
    fs::remove(path, ec);
}

#endif  // !_WIN32

// Watchdog fires on_fence exactly once when heartbeats stop.
TEST(shared_chain_lock, watchdog_fences_on_stall) {
    std::atomic<int> fired{0};
    sc::fence_watchdog wd{80ms, 20ms, [&fired] { fired++; }};
    wd.start();
    // No heartbeats: it should fence within a few check intervals.
    std::this_thread::sleep_for(400ms);
    EXPECT_TRUE(wd.fenced());
    EXPECT_EQ(fired.load(), 1);  // fires once only
    wd.stop();
}

// Watchdog does not fire while heartbeats keep arriving.
TEST(shared_chain_lock, watchdog_held_off_by_heartbeats) {
    std::atomic<int> fired{0};
    sc::fence_watchdog wd{120ms, 20ms, [&fired] { fired++; }};
    wd.start();
    for (int i = 0; i < 10; i++) {  // ~400ms of regular heartbeats
        std::this_thread::sleep_for(40ms);
        wd.heartbeat();
    }
    EXPECT_FALSE(wd.fenced());
    EXPECT_EQ(fired.load(), 0);
    wd.stop();
}
