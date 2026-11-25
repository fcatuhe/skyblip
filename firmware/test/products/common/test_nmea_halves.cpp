// The EFB's own-ship sentences at 2 Hz: the whole second they report, and its withdrawal.
#include <cstdint>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "test/support/half_second_feed.h"
#include "test/support/nmea_stream.h"
#include "test/support/product_rig.h"

using namespace skyblip;

// The pinned case: a fix withdrawn at .500 left the EFB reading the last .000 as a valid fix.
TEST_CASE("nmea: a fix withdrawn at .500 stops the EFB's GPRMC at once") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.raise_link();
    uint32_t updates = 0;
    for (uint32_t second = 1; second <= 3; second++)
        for (uint32_t half = 0; half < 2; half++) solve(rig, second, half, 25000, updates);
    REQUIRE(rig.link_up());
    REQUIRE(count_of(rig, "$GPRMC") > 0);

    solve(rig, 4, 0, 25000, updates);
    deliver(rig, Solution{4, 500, 25000, kFeedLatencyMs, false}, updates);
    rig.platform.link().clear();
    rig.run(4900, 8000);
    CHECK(count_of(rig, "$GPRMC") == 0);
    CHECK(count_of(rig, "$GPGGA") == 0);
}

// The pinned case: a pass on its own 1 s cadence held the whole second until 300 ms after it landed.
TEST_CASE("nmea: the EFB's GPRMC leaves within a quarter second of its whole second") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.raise_link();
    rig.run(1300, 1300);
    uint32_t updates = 0;
    std::vector<uint32_t> lags_ms;
    for (uint32_t second = 2; second <= 12; second++) {
        for (uint32_t half = 0; half < 2; half++) {
            const uint32_t landed_ms = second * 1000 + half * kHalfSecondFeedMs + kFeedLatencyMs;
            int sent = count_of(rig, "$GPRMC");
            solve(rig, second, half, 25000, updates);
            for (uint32_t at_ms = landed_ms; at_ms < landed_ms + kHalfSecondFeedMs; at_ms += 10) {
                if (at_ms > landed_ms) rig.run(at_ms, at_ms);
                if (count_of(rig, "$GPRMC") > sent && second >= 4)
                    lags_ms.push_back(at_ms - second * 1000);
                sent = count_of(rig, "$GPRMC");
            }
        }
    }
    REQUIRE(lags_ms.size() == 9);
    for (const uint32_t lag_ms : lags_ms) CHECK(lag_ms <= 250);
}

// A pass that waits on the whole second must not wait on one the receiver lost.
TEST_CASE("nmea: a lost whole second holds the tablet's heartbeat a quarter second, no more") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.raise_link();
    uint32_t updates = 0;
    for (uint32_t second = 1; second <= 5; second++)
        for (uint32_t half = 0; half < 2; half++) {
            solve(rig, second, half, 25000, updates);
            const uint32_t landed_ms = second * 1000 + half * kHalfSecondFeedMs + kFeedLatencyMs;
            rig.run(landed_ms + 10, landed_ms + kHalfSecondFeedMs - 10, 10);
        }
    rig.platform.link().clear();
    solve(rig, 6, 1, 25000, updates);
    rig.run(6580, 7060, 10);
    CHECK(count_of(rig, "$PFLAU") == 1);
}
