// Where an aircraft will be if it keeps doing what it is doing: one path, drawn and alarmed on.
#include "core/flight/arc.h"
#include "core/units/units.h"
#include "core/util/intmath.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::flight;

namespace {

constexpr uint32_t kStepMs = 1000;
constexpr int32_t kThermalSpeedMmS = 23150;

Motion flying(int track_deg, int32_t speed_mm_s, int16_t turn_dps) {
    Motion m{};
    m.speed_mm_s = speed_mm_s;
    m.track_cdeg = track_deg * 100;
    m.turn_cdps = static_cast<int16_t>(turn_dps * 100);
    m.turning = true;
    return m;
}

Position after(Motion m, int seconds, uint32_t step_ms = kStepMs) {
    Arc arc(m, step_ms);
    Position p = arc.here();
    for (uint32_t elapsed = 0; elapsed < static_cast<uint32_t>(seconds) * 1000u; elapsed += step_ms)
        p = arc.advance();
    return p;
}

}  // namespace

TEST_CASE("arc: an aircraft not turning flies a straight ray") {
    const Position p = after(flying(0, 30000, 0), 60);
    CHECK(p.north_m == 1800);
    CHECK(p.east_m == 0);

    const Position east = after(flying(90, 30000, 0), 60);
    CHECK(east.east_m == 1800);
    CHECK(east.north_m == 0);
}

TEST_CASE("arc: a turn flies the radius its rate and speed describe") {
    // 23.15 m/s at 12 deg/s is radius = speed / rate = 111 m, so 221 m across at the half turn.
    const Position half = after(flying(0, kThermalSpeedMmS, 12), 15);
    const int32_t diameter = static_cast<int32_t>(hypotenuse(half.north_m, half.east_m));
    CHECK(diameter > 213);
    CHECK(diameter < 229);
    CHECK(half.east_m > 213);
    CHECK(half.north_m < 8);
    CHECK(half.north_m > -8);
}

TEST_CASE("arc: a full circle comes back to where it started") {
    const Position full = after(flying(0, kThermalSpeedMmS, 12), 30);
    CHECK(hypotenuse(full.north_m, full.east_m) < 8);
}

TEST_CASE("arc: a left turn mirrors a right turn") {
    const Position right = after(flying(0, kThermalSpeedMmS, 13), 7);
    const Position left = after(flying(0, kThermalSpeedMmS, -13), 7);
    CHECK(right.north_m == left.north_m);
    CHECK(right.east_m == -left.east_m);
}

TEST_CASE("arc: the step is a cadence, not a shape") {
    const Motion m = flying(30, kThermalSpeedMmS, 9);
    const Position coarse = after(m, 20, 2000);
    const Position fine = after(m, 20, 500);
    CHECK(hypotenuse(coarse.north_m - fine.north_m, coarse.east_m - fine.east_m) < 10);
}

TEST_CASE("arc: a climb is carried with the path") {
    Motion m = flying(0, 30000, 0);
    m.climb_mm_s = 3000;
    m.climbing = true;
    CHECK(after(m, 10).up_m == 30);
    m.climbing = false;
    CHECK(after(m, 10).up_m == 0);
}

TEST_CASE("arc: a turn rate no aeroplane holds is clamped, not believed") {
    CHECK(clamped_turn_cdps(9000) == kTurnCeilingCdps);
    CHECK(clamped_turn_cdps(-9000) == -kTurnCeilingCdps);
    CHECK(clamped_turn_cdps(1300) == 1300);

    const Position wild = after(flying(0, kThermalSpeedMmS, 200), 20);
    const Position clamped = after(flying(0, kThermalSpeedMmS, kTurnCeilingCdps / 100), 20);
    CHECK(wild.north_m == clamped.north_m);
    CHECK(wild.east_m == clamped.east_m);
}

TEST_CASE("arc: a target with no velocity of its own does not fly anywhere") {
    model::AircraftObservation observation{};
    observation.speed_valid = false;
    observation.track_c9 = to_cordic9(CentiDegrees(18000)).value;
    const Position p = after(motion_of(observation, 0, false), 60);
    CHECK(p.north_m == 0);
    CHECK(p.east_m == 0);
}
