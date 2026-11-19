// Own-ship at 2 Hz: each solution moves the state ADS-L sends, the whole second the rest.
#include <cstdint>

#include "core/flight/state.h"
#include "core/gnss/nmea.h"
#include "doctest/doctest.h"
#include "test/support/half_second_feed.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {
constexpr uint32_t kBaseUtcS = Rig::kBaseUtcS;
constexpr uint32_t kHalfMs = kHalfSecondFeedMs;
constexpr uint32_t kLatencyMs = kFeedLatencyMs;
}  // namespace

// The fix's instant is its own quarter, from the PPS edge, so G.1.16 can age it to the burst.
TEST_CASE("product: a .500 solution is dated at its quarter, not at its arrival") {
    Rig rig{kBarometerByHand};
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t updates = 0;
    solve(rig, 3, 0, 0, updates);
    solve(rig, 3, 1, 0, updates);

    CHECK(rig.state().own.utc_s == kBaseUtcS + 3);
    CHECK(rig.state().own.utc_ms == 500);
    CHECK(rig.state().own.fix_at_ms == 3500);
    CHECK(rig.state().gnss.solution_phase_ms == kLatencyMs);
}

// The pinned case: a whole second a second late was dated at the next edge and anchored it.
TEST_CASE("product: a solution that lands a second late keeps its own second, and anchors none") {
    Rig rig{kBarometerByHand};
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t updates = 0;
    for (uint32_t second = 1; second <= 3; second++) solve(rig, second, 0, 0, updates);
    REQUIRE(rig.state().clock.utc_s == kBaseUtcS + 3);

    deliver(rig, Solution{4, 0, 0, 1000 + kLatencyMs}, updates);
    CHECK(rig.state().own.fix_at_ms == 4000);
    CHECK(rig.state().clock.utc_s == kBaseUtcS + 5);

    deliver(rig, Solution{5, 500, 0, 1000 + kLatencyMs}, updates);
    CHECK(rig.state().own.fix_at_ms == 5500);
}

// A solution not on its own ms of .000 still opens the second: the flight model must not stall.
TEST_CASE("product: the first solution of each UTC second is its whole second, stamp or not") {
    Rig rig{kBarometerByHand};
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t updates = 0;
    constexpr int32_t kFlyingMmS = 30000;
    for (uint32_t second = 1; second <= 3; second++)
        for (uint32_t half = 0; half < 2; half++)
            deliver(rig, Solution{second, half * kHalfMs + 4, kFlyingMmS}, updates);
    CHECK(flight::airborne(rig.state().own.flight_state));
}

// A .500 speed spike is position for ADS-L, never a takeoff: the flight model reads the second.
TEST_CASE("product: the flight state is decided on whole seconds, as it was at 1 Hz") {
    Rig rig{kBarometerByHand};
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t updates = 0;
    constexpr int32_t kFlyingMmS = 30000;
    for (uint32_t second = 1; second <= 5; second++) {
        solve(rig, second, 0, 0, updates);
        solve(rig, second, 1, kFlyingMmS, updates);
        CHECK(rig.state().own.speed_mm_s == kFlyingMmS);
        CHECK_FALSE(flight::airborne(rig.state().own.flight_state));
    }
    CHECK(rig.state().flight.gnss_solutions == 10);

    for (uint32_t second = 6; second <= 7; second++)
        for (uint32_t half = 0; half < 2; half++) solve(rig, second, half, kFlyingMmS, updates);
    CHECK(flight::airborne(rig.state().own.flight_state));
}

// The flight log and the EFB's GPRMC read own-ship as its whole second left it, dated by it.
TEST_CASE("product: own-ship's whole second stands while the .500 solution after it moves") {
    Rig rig{kBarometerByHand};
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t updates = 0;
    solve(rig, 2, 0, 0, updates);
    const int32_t whole_latitude_1e7 = rig.state().own.latitude_1e7;
    solve(rig, 2, 1, 0, updates);

    CHECK(rig.state().own.latitude_1e7 != whole_latitude_1e7);
    CHECK(rig.state().own_second.latitude_1e7 == whole_latitude_1e7);
    CHECK(rig.state().own_second.utc_s == kBaseUtcS + 2);
    CHECK(rig.state().own_second.utc_ms == 0);
}

// A whole second delivered later than a quarter breaks the receiver's stated latency: it anchors
// nothing a position may be dated by, and the next one inside the quarter does.
TEST_CASE("product: only a fix delivered inside its quarter anchors the clock positions trust") {
    Rig rig{kBarometerByHand};
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t updates = 0;
    deliver(rig, Solution{1, 0, 0, 300}, updates);
    deliver(rig, Solution{1, 500, 0, 320}, updates);
    CHECK_FALSE(rig.state().clock.utc_anchor_fixed);
    CHECK_FALSE(rig.state().own.fix_dated);

    solve(rig, 2, 0, 0, updates);
    solve(rig, 2, 1, 0, updates);
    CHECK(rig.state().clock.utc_anchor_fixed);
    CHECK(rig.state().own.fix_dated);
    CHECK(rig.state().own.fix_at_ms == 2500);
}

// The pinned case: an unfixed stamp a second ahead anchored the clock, and every fix looked stale.
TEST_CASE("product: the first fix re-anchors a clock an unfixed stamp anchored a second off") {
    Rig rig{kBarometerByHand};
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t updates = 0;
    for (uint32_t second = 1; second <= 3; second++)
        deliver(rig, Solution{second, 0, 0, kLatencyMs, false, 1}, updates);
    REQUIRE(rig.state().clock.utc_s == kBaseUtcS + 4);

    for (uint32_t second = 4; second <= 5; second++)
        for (uint32_t half = 0; half < 2; half++) solve(rig, second, half, 0, updates);
    CHECK(rig.state().clock.utc_s == kBaseUtcS + 5);
    CHECK(rig.state().clock.utc_anchor_fixed);
    CHECK(rig.state().own.fix_at_ms == 5500);
    CHECK(rig.state().own.fix_dated);
}

// At 9600 the fix burst alone is 333 ms of line, so a timely fix lands about 400 ms after its edge.
TEST_CASE("product: a fix inside its part's own delivery time anchors the clock, at 9600 too") {
    Rig rig{kBarometerByHand};
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t updates = 0;
    deliver(rig, Solution{1, 0, 0, 403, true, 0, 403}, updates);
    CHECK(rig.state().clock.utc_anchor_fixed);

    Rig fast{kBarometerByHand};
    REQUIRE(fast.setup() == Status::Ok);
    updates = 0;
    deliver(fast, Solution{1, 0, 0, 300, true, 0, 98}, updates);
    CHECK_FALSE(fast.state().clock.utc_anchor_fixed);
}

// A whole second drained behind an older pair arrived when the host got to it, not when it landed.
TEST_CASE("product: a whole second drained behind an older pair anchors nothing") {
    Rig rig{kBarometerByHand};
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t updates = 0;
    deliver(rig, Solution{1, 0, 0, kLatencyMs, true, 0, 0, true}, updates);
    CHECK(rig.state().clock.utc_s == 0);
    CHECK_FALSE(rig.state().clock.utc_anchor_fixed);
}

// The pinned case: a part stamping every quarter 4 ms late anchored nothing and logged nothing.
TEST_CASE("product: stamps 4 ms off their quarters anchor, date and log as the quarters") {
    Rig rig{kBarometerByHand};
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t updates = 0;
    for (uint32_t second = 1; second <= 3; second++)
        for (uint32_t half = 0; half < 2; half++)
            deliver(rig, Solution{second, half * kHalfMs + 4, 0}, updates);
    CHECK(rig.state().clock.utc_anchor_fixed);
    CHECK(rig.state().own.fix_dated);
    CHECK(rig.state().own_second.utc_s == kBaseUtcS + 3);
    CHECK(rig.state().own_second.utc_ms == 4);
}
