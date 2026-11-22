// Where an aircraft will be if it keeps doing what it is doing: one path, drawn and alarmed on.
#include "core/flight/arc.h"
#include "doctest/doctest.h"
#include "protocols/common/geometry.h"

using namespace skyblip;
using namespace skyblip::flight;

namespace {

constexpr int32_t kStepS = 1;
constexpr int32_t kThermalSpeedMmS = 23150;

Motion flying(int track_deg, int32_t speed_mm_s, int16_t turn_dps) {
    Motion motion{};
    motion.speed_mm_s = speed_mm_s;
    motion.track_cdeg = track_deg * 100;
    motion.turn_cdps = static_cast<int16_t>(turn_dps * 100);
    motion.turning = true;
    return motion;
}

Position after(Motion motion, int seconds, int32_t step_s = kStepS) {
    Arc arc(motion, step_s);
    Position position{};
    for (int elapsed_s = 0; elapsed_s < seconds; elapsed_s += step_s) position = arc.advance();
    return position;
}

}  // namespace

TEST_CASE("arc: an aircraft not turning flies a straight ray") {
    const Position position = after(flying(0, 30000, 0), 60);
    CHECK(position.north_m == 1800);
    CHECK(position.east_m == 0);

    const Position east = after(flying(90, 30000, 0), 60);
    CHECK(east.east_m == 1800);
    CHECK(east.north_m == 0);
}

TEST_CASE("arc: a turn flies the radius its rate and speed describe" *
          doctest::test_suite("main")) {
    // 23.15 m/s at 12 deg/s is radius = speed / rate = 111 m, so 221 m across at the half turn.
    const Position half = after(flying(0, kThermalSpeedMmS, 12), 15);
    const int32_t diameter = static_cast<int32_t>(wire::hypotenuse(half.north_m, half.east_m));
    CHECK(diameter > 213);
    CHECK(diameter < 229);
    CHECK(half.east_m > 213);
    CHECK(half.north_m < 8);
    CHECK(half.north_m > -8);
}

TEST_CASE("arc: a full circle comes back to where it started") {
    const Position full = after(flying(0, kThermalSpeedMmS, 12), 30);
    CHECK(wire::hypotenuse(full.north_m, full.east_m) < 8);
}

TEST_CASE("arc: a left turn mirrors a right turn") {
    const Position right = after(flying(0, kThermalSpeedMmS, 13), 7);
    const Position left = after(flying(0, kThermalSpeedMmS, -13), 7);
    CHECK(right.north_m == left.north_m);
    CHECK(right.east_m == -left.east_m);
}

TEST_CASE("arc: the step is a cadence, not a shape") {
    const Motion motion = flying(30, kThermalSpeedMmS, 9);
    const Position coarse = after(motion, 20, 2);
    const Position fine = after(motion, 20, 1);
    CHECK(wire::hypotenuse(coarse.north_m - fine.north_m, coarse.east_m - fine.east_m) < 10);
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

// Positions are int32 millimetres, so the fastest speed an AircraftObservation holds must not wrap.
TEST_CASE("arc: the fastest speed a target can report flies two minutes without wrapping") {
    const Position far = after(flying(0, 16383750, 0), 120, 5);
    CHECK(far.north_m == 1966050);
    CHECK(far.east_m == 0);
}
