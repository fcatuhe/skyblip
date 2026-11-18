// Own-ship at 4 Hz: each quarter solution moves the state ADS-L sends, the whole second the rest.
#include <cstdint>

#include "core/flight/state.h"
#include "core/gnss/nmea.h"
#include "doctest/doctest.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

constexpr uint32_t kBaseUtcS = Rig::kBaseUtcS;
constexpr uint32_t kQuarterMs = 250;
constexpr uint32_t kLatencyMs = 70;

// One solution, landing kLatencyMs after the quarter it names, as the L76K delivers it.
void solve(Rig& rig, uint32_t second, uint32_t quarter, int32_t speed_mm_s, uint32_t& updates) {
    gnss::GnssSolution solution{};
    solution.fix_valid = true;
    solution.utc_valid = true;
    solution.utc_s = kBaseUtcS + second;
    solution.utc_ms = static_cast<uint16_t>(quarter * kQuarterMs);
    solution.latitude_1e7 = 485000000 + static_cast<int32_t>(second * 4 + quarter) * 100;
    solution.longitude_1e7 = 85000000;
    solution.altitude_hae_mm = 1000000;
    solution.altitude_msl_valid = true;
    solution.speed_mm_s = speed_mm_s;
    solution.track_cdeg = 0;
    solution.hdop_e2 = 100;
    solution.vdop_e2 = 150;
    solution.satellites = 10;
    solution.sentences = ++updates;
    rig.product.bus().gnss.push(solution);
    const uint32_t at_ms = second * 1000 + quarter * kQuarterMs + kLatencyMs;
    rig.run(at_ms, at_ms);
}

}  // namespace

// The fix's instant is its own quarter, from the PPS edge, so G.1.16 can age it to the burst.
TEST_CASE("product: a quarter solution is dated at its quarter, not at its arrival") {
    Rig rig{kBarometerByHand};
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t updates = 0;
    solve(rig, 3, 0, 0, updates);
    solve(rig, 3, 3, 0, updates);

    CHECK(rig.state().own.utc_s == kBaseUtcS + 3);
    CHECK(rig.state().own.utc_ms == 750);
    CHECK(rig.state().own.fix_at_ms == 3750);
    CHECK(rig.state().gnss.solution_phase_ms == kLatencyMs);
}

// A quarter speed spike is position for ADS-L, never a takeoff: the flight model reads the second.
TEST_CASE("product: the flight state is decided on whole seconds, as it was at 1 Hz") {
    Rig rig{kBarometerByHand};
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t updates = 0;
    constexpr int32_t kFlyingMmS = 30000;
    for (uint32_t second = 1; second <= 5; second++) {
        solve(rig, second, 0, 0, updates);
        for (uint32_t quarter = 1; quarter < 4; quarter++)
            solve(rig, second, quarter, kFlyingMmS, updates);
        CHECK(rig.state().own.speed_mm_s == kFlyingMmS);
        CHECK_FALSE(flight::airborne(rig.state().own.flight_state));
    }
    CHECK(rig.state().flight.gnss_solutions == 20);

    for (uint32_t second = 6; second <= 7; second++)
        for (uint32_t quarter = 0; quarter < 4; quarter++)
            solve(rig, second, quarter, kFlyingMmS, updates);
    CHECK(flight::airborne(rig.state().own.flight_state));
}
