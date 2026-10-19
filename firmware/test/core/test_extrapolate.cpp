// F3. A position sent under another instant's timestamp is a lie of 50 m at 50 m/s over a second.
#include <cmath>

#include "core/flight/extrapolate.h"
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/protocol/adsl.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::flight;
using namespace skyblip::protocol;

namespace {

constexpr double kMetresPer1e7 = 0.0011132 * 10;  // 1e-7 deg of latitude, metres

model::Ownship flying(double latitude_deg, double longitude_deg, double speed_mps, double track_deg,
                      double climb_mps = 0.0, double turn_dps = 0.0) {
    model::Ownship own{};
    own.fix_valid = true;
    own.latitude_1e7 = static_cast<int32_t>(latitude_deg * 1e7);
    own.longitude_1e7 = static_cast<int32_t>(longitude_deg * 1e7);
    own.altitude_hae_mm = 1200000;
    own.altitude_msl_mm = 1155000;
    own.speed_mm_s = static_cast<int32_t>(speed_mps * 1000);
    own.track_cdeg = static_cast<int32_t>(track_deg * 100);
    own.climb_mm_s = static_cast<int32_t>(climb_mps * 1000);
    own.climb_valid = climb_mps != 0.0;
    own.turn_cdps = static_cast<int16_t>(turn_dps * 100);
    return own;
}

double north_m(int32_t from_latitude_1e7, int32_t to_latitude_1e7) {
    return (to_latitude_1e7 - from_latitude_1e7) * kMetresPer1e7;
}

double east_m(const model::Ownship& from, int32_t to_longitude_1e7) {
    const double coslat = std::cos(from.latitude_1e7 / 1e7 * M_PI / 180.0);
    return (to_longitude_1e7 - from.longitude_1e7) * kMetresPer1e7 * coslat;
}

}  // namespace

TEST_CASE("extrapolate: a straight leg moves the fix along its own track") {
    const model::Ownship own = flying(48.5, 8.5, 40.0, 90.0);
    const Prediction at = extrapolate(own, 1000);
    REQUIRE(at.valid);
    // Due east at 40 m/s for one second: 40 m of easting and nothing else.
    CHECK(east_m(own, at.longitude_1e7) == doctest::Approx(40.0).epsilon(0.01));
    CHECK(north_m(own.latitude_1e7, at.latitude_1e7) == doctest::Approx(0.0).epsilon(0.01));
    CHECK(at.track_cdeg == own.track_cdeg);
    CHECK(at.altitude_hae_mm == own.altitude_hae_mm);

    // Half the time is half the distance, and the model runs backwards as well:
    // measuring the residual means predicting into the past.
    CHECK(east_m(own, extrapolate(own, 500).longitude_1e7) == doctest::Approx(20.0).epsilon(0.02));
    CHECK(east_m(own, extrapolate(own, -1000).longitude_1e7) ==
          doctest::Approx(-40.0).epsilon(0.01));
    CHECK(extrapolate(own, 0).latitude_1e7 == own.latitude_1e7);
}

TEST_CASE("extrapolate: flying east across the antimeridian comes out at 180 west") {
    const model::Ownship own = flying(10.0, 179.9999, 60.0, 90.0);
    const Prediction at = extrapolate(own, 1000);
    REQUIRE(at.valid);
    CHECK(at.longitude_1e7 < -1799990000);
    CHECK(prediction_residual_m(at, own.latitude_1e7, own.longitude_1e7, own.altitude_hae_mm) ==
          doctest::Approx(60).epsilon(0.05));
}

// A neighbour is carried by the three things its burst states: position, ground speed and track.
TEST_CASE("extrapolate: a reported target moves along its reported track") {
    const model::Ownship own = flying(48.5, 8.5, 40.0, 90.0);
    model::AircraftObservation observation{};
    observation.position_valid = true;
    observation.speed_valid = true;
    observation.latitude_1e7 = own.latitude_1e7;
    observation.longitude_1e7 = own.longitude_1e7;
    observation.altitude_m = 1200;
    observation.speed_q = 40 * 4;
    observation.track_c9 = 128;  // due east

    const Prediction at = extrapolate(observation, 1000);
    REQUIRE(at.valid);
    CHECK(east_m(own, at.longitude_1e7) == doctest::Approx(40.0).epsilon(0.01));
    CHECK(north_m(own.latitude_1e7, at.latitude_1e7) == doctest::Approx(0.0).epsilon(0.01));
    CHECK(at.altitude_hae_mm ==
          observation.altitude_m * 1000);  // no climb reported, no climb invented

    observation.climb_valid = true;
    observation.climb_e8 = 8 * 2;  // 2 m/s
    CHECK(extrapolate(observation, 1000).altitude_hae_mm == 1202000);

    // Relayed traffic often arrives as a position and nothing else: there is no
    // model to run, and the last known position is the only honest answer.
    observation.speed_valid = false;
    const Prediction still = extrapolate(observation, 1000);
    CHECK_FALSE(still.valid);
    CHECK(still.latitude_1e7 == observation.latitude_1e7);
    CHECK(still.longitude_1e7 == observation.longitude_1e7);
}

// The reason for the half-turn-before, half-turn-after split
// (oss/nrf52-ogn-tracker src/ogn.h:1735-1790): a glider in a thermal spends the
// whole second turning, and putting it on the tangent puts it outside its own
// circle. Four gliders in one core is exactly where a metre matters.
TEST_CASE("extrapolate: a circling glider is predicted on its arc, not on the tangent") {
    const model::Ownship own = flying(48.5, 8.5, 40.0, 0.0, 0.0, 20.0);
    const Prediction at = extrapolate(own, 1000);
    REQUIRE(at.valid);

    // 40 m/s through 20 deg/s is a 114.6 m radius: 39.2 m north, 6.9 m east.
    const double radius_m = 40.0 / (20.0 * M_PI / 180.0);
    const double arc_north = radius_m * std::sin(20.0 * M_PI / 180.0);
    const double arc_east = radius_m * (1.0 - std::cos(20.0 * M_PI / 180.0));
    CHECK(north_m(own.latitude_1e7, at.latitude_1e7) == doctest::Approx(arc_north).epsilon(0.02));
    CHECK(east_m(own, at.longitude_1e7) == doctest::Approx(arc_east).epsilon(0.05));

    // The tangent it is not: straight ahead puts it 6.9 m off the circle.
    model::Ownship straight = own;
    straight.turn_cdps = 0;
    CHECK(east_m(straight, extrapolate(straight, 1000).longitude_1e7) == doctest::Approx(0.0));

    // And the track goes with it, to the hundredth of a degree the gyro resolves.
    CHECK(at.track_cdeg == doctest::Approx(2000).epsilon(0.01));
    // Turning left through north wraps rather than going negative.
    model::Ownship left = flying(48.5, 8.5, 40.0, 0.0, 0.0, -20.0);
    CHECK(extrapolate(left, 1000).track_cdeg == doctest::Approx(34000).epsilon(0.01));
}

TEST_CASE("extrapolate: the climb carries both altitudes, and only when it is known") {
    const model::Ownship climbing = flying(48.5, 8.5, 30.0, 45.0, 2.0);
    const Prediction at = extrapolate(climbing, 1000);
    REQUIRE(at.valid);
    CHECK(at.altitude_hae_mm == climbing.altitude_hae_mm + 2000);
    CHECK(at.altitude_msl_mm == climbing.altitude_msl_mm + 2000);
    CHECK(extrapolate(climbing, -500).altitude_hae_mm == climbing.altitude_hae_mm - 1000);

    // G.1.9's "unavailable" is not zero, and neither is it a level prediction we
    // are entitled to make: without a vertical rate the altitude stands still.
    model::Ownship unknown = climbing;
    unknown.climb_valid = false;
    CHECK(extrapolate(unknown, 1000).altitude_hae_mm == unknown.altitude_hae_mm);
}

// The bound, and which side of it we chose: past it the fix passes through
// untouched and says so, so the caller can date it as the fix rather than
// dressing a guess as a prediction.
TEST_CASE("extrapolate: a gap longer than the bound is not predicted at all") {
    const model::Ownship own = flying(48.5, 8.5, 50.0, 90.0, 1.0);

    const Prediction inside = extrapolate(own, kExtrapolationCeilingMs);
    CHECK(inside.valid);
    CHECK(inside.longitude_1e7 != own.longitude_1e7);

    const Prediction outside = extrapolate(own, kExtrapolationCeilingMs + 1);
    CHECK_FALSE(outside.valid);
    CHECK(outside.latitude_1e7 == own.latitude_1e7);
    CHECK(outside.longitude_1e7 == own.longitude_1e7);
    CHECK(outside.altitude_hae_mm == own.altitude_hae_mm);
    CHECK(outside.track_cdeg == own.track_cdeg);

    CHECK_FALSE(extrapolate(own, -(kExtrapolationCeilingMs + 1)).valid);

    // A position nobody has is not extrapolated either.
    model::Ownship blind = own;
    blind.fix_valid = false;
    CHECK_FALSE(extrapolate(blind, 500).valid);
}

// OGN's PredResid (src/ogn.h:1424-1430). The point is that it is measured
// against a fix that actually arrived, so a model that has stopped describing
// the aircraft says so on the bench instead of in the air.
TEST_CASE("extrapolate: the residual is what the model missed, in metres") {
    const model::Ownship own = flying(48.5, 8.5, 40.0, 0.0, 2.0);
    const Prediction at = extrapolate(own, 1000);
    REQUIRE(at.valid);

    // The fix that arrives exactly where the model put it costs nothing.
    CHECK(prediction_residual_m(at, at.latitude_1e7, at.longitude_1e7, at.altitude_hae_mm) == 0);

    // Five metres north and three up of the prediction is eight metres of miss:
    // north, east and vertical, summed absolute, as OGN sums them.
    const int32_t five_north = at.latitude_1e7 + static_cast<int32_t>(5.0 / kMetresPer1e7);
    CHECK(prediction_residual_m(at, five_north, at.longitude_1e7, at.altitude_hae_mm + 3000) ==
          doctest::Approx(8).epsilon(0.2));

    // An aircraft flying the model it is given is predicted to within a metre a
    // second; one that rolls into a turn the model never saw is not, and the
    // residual is the difference between the two.
    const model::Ownship turning = flying(48.5, 8.5, 40.0, 0.0, 2.0, 25.0);
    const Prediction ignored_turn = extrapolate(own, 1000);
    const Prediction with_turn = extrapolate(turning, 1000);
    const uint32_t missed = prediction_residual_m(
        ignored_turn, with_turn.latitude_1e7, with_turn.longitude_1e7, with_turn.altitude_hae_mm);
    CHECK(missed > 5);
}

// F3. The burst leaves a second after its solution: as it stands, it is 50 m behind at 50 m/s.
TEST_CASE("adsl: the transmitted position is the position at the quarter second it names") {
    model::Ownship own{};
    own.fix_valid = true;
    own.latitude_1e7 = 485000000;
    own.longitude_1e7 = 85000000;
    own.altitude_hae_mm = 1200000;
    own.speed_mm_s = 50000;
    own.track_cdeg = 9000;  // due east
    own.climb_mm_s = 2000;
    own.climb_valid = true;
    own.vdop_e2 = 150;
    own.utc_s = 1000;  // 1000 % 15 == 10 s into the timestamp cycle

    AdslPacket at_fix{};
    from_own(at_fix, own, 0xABCDEF, 6, 4);
    CHECK(int(at_fix.timestamp) == 40);  // 10 s, quarter zero

    // A burst 600 ms later names the quarter at 500 ms, and the position goes to it.
    AdslPacket in_flight{};
    from_own(in_flight, own, 0xABCDEF, 6, 4, BurstInstant{own.utc_s, 600, 600});
    CHECK(int(in_flight.timestamp) == 42);
    CHECK(in_flight.altitude_m() == at_fix.altitude_m() + 1);
    CHECK(in_flight.latitude_1e7() == at_fix.latitude_1e7());

    // 25 m of easting at 50 m/s over 500 ms, inside the 2.4 m the longitude field quantises to.
    const double east_m = (in_flight.longitude_1e7() - at_fix.longitude_1e7()) * 0.011132 * 0.6626;
    CHECK(east_m == doctest::Approx(25.0).epsilon(0.1));
}

// The bound, and the side of it we chose: a gap the model cannot cover is not
// filled with a guess. The last fix goes out unmoved, dated when it was solved,
// so the position and the timestamp still describe the same instant.
TEST_CASE("adsl: past the extrapolation bound the fix goes out dated as the fix") {
    model::Ownship own{};
    own.fix_valid = true;
    own.latitude_1e7 = 485000000;
    own.longitude_1e7 = 85000000;
    own.altitude_hae_mm = 1200000;
    own.speed_mm_s = 50000;
    own.track_cdeg = 9000;
    own.utc_s = 1000;

    AdslPacket at_fix{};
    from_own(at_fix, own, 0xABCDEF, 6, 4);

    const int32_t bound = kExtrapolationCeilingMs;
    AdslPacket inside{};
    from_own(inside, own, 0xABCDEF, 6, 4, BurstInstant{own.utc_s, bound, bound});
    CHECK(inside.longitude_1e7() != at_fix.longitude_1e7());
    CHECK(int(inside.timestamp) == timestamp_code(own.utc_s, bound));

    const int32_t past = bound + static_cast<int32_t>(kTimestampQuarterMs);
    AdslPacket beyond{};
    from_own(beyond, own, 0xABCDEF, 6, 4, BurstInstant{own.utc_s, past, past});
    CHECK(beyond.longitude_1e7() == at_fix.longitude_1e7());
    CHECK(beyond.latitude_1e7() == at_fix.latitude_1e7());
    CHECK(int(beyond.timestamp) == int(at_fix.timestamp));
}

// Quarter seconds inside a 15 s cycle: six bits, and the wrap is part of the
// encoding rather than an edge case somebody has to remember.
TEST_CASE("adsl: the timestamp is quarter seconds inside a fifteen second cycle") {
    CHECK(int(timestamp_code(0, 0)) == 0);
    CHECK(int(timestamp_code(14, 0)) == 56);
    CHECK(int(timestamp_code(15, 0)) == 0);
    CHECK(int(timestamp_code(14, 999)) == 59);
    CHECK(int(timestamp_code(14, 1000)) == 0);  // wraps into the next cycle
    CHECK(int(timestamp_code(0, -250)) == 59);  // and backwards out of it
    for (uint32_t utc_s = 0; utc_s < 60; utc_s++)
        for (int32_t lead = -1000; lead <= 1000; lead += 125)
            CHECK(int(timestamp_code(utc_s, lead)) < 60);
}
