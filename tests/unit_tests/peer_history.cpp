// Tests for the served-history checks in src/cryptonote_protocol/peer_history.h.

#include "gtest/gtest.h"

#include "cryptonote_protocol/peer_history.h"

#include <cstdint>
#include <limits>

using cryptonote::history_covers;
using cryptonote::PEER_HISTORY_MARGIN;

namespace {
constexpr uint64_t MAX = std::numeric_limits<uint64_t>::max();
}

// A full peer (history 0) covers every height.
TEST(peer_history, full_peer_covers_everything) {
    EXPECT_TRUE(history_covers(0, 0, 0));
    EXPECT_TRUE(history_covers(0, 1'000'000, 0));
    EXPECT_TRUE(history_covers(0, MAX, 0));
}

// A peer ahead of us covers a height when its history reaches back to it plus the margin.
TEST(peer_history, boundary_when_peer_is_ahead) {
    EXPECT_TRUE(history_covers(1000, 2000, 1000 + PEER_HISTORY_MARGIN));
    EXPECT_FALSE(history_covers(1000, 2000, 1000 + PEER_HISTORY_MARGIN - 1));
    EXPECT_FALSE(history_covers(1000, 200'000, 1));
}

// A peer at or below our height still needs the margin between its history and the gap.
TEST(peer_history, boundary_when_peer_is_behind) {
    EXPECT_TRUE(history_covers(2000, 1950, PEER_HISTORY_MARGIN - 50));
    EXPECT_FALSE(history_covers(2000, 1950, PEER_HISTORY_MARGIN - 51));
    EXPECT_TRUE(history_covers(2000, 2000, PEER_HISTORY_MARGIN));
    EXPECT_FALSE(history_covers(2000, 2000, PEER_HISTORY_MARGIN - 1));
}

// Values near the top of the range must not wrap around.
TEST(peer_history, no_wraparound) {
    EXPECT_TRUE(history_covers(1000, 200'000, MAX - 10));
    EXPECT_TRUE(history_covers(MAX - 5, MAX, MAX));
    EXPECT_FALSE(history_covers(0, MAX, 200));
    EXPECT_FALSE(history_covers(0, MAX - 50, 200));
}
