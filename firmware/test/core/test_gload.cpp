// What the airframe is pulling, in the sense the ball beside it already reads.
#include "core/flight/gload.h"
#include "doctest/doctest.h"

using namespace skyblip;
using flight::SpecificForce;

namespace {

constexpr SpecificForce level{0, 1000, 0};

}  // namespace

TEST_CASE("gload: level flight is one g up and nothing across") {
    flight::GMeter meter;
    CHECK_FALSE(meter.valid(0));

    meter.observe(level, 1000);

    CHECK(meter.valid(1000));
    CHECK(meter.now().normal_mg == 1000);
    CHECK(meter.now().lateral_mg == 0);
    CHECK(meter.now().longitudinal_mg == 0);
    CHECK_FALSE(meter.valid(1000 + flight::kIndicatedStaleMs));
}

// A force to the right throws a loose object left, which is where the ball goes.
TEST_CASE("gload: lateral reads the way the ball reads") {
    flight::GMeter meter;
    meter.observe(SpecificForce{300, 1000, 0}, 1000);
    CHECK(meter.now().lateral_mg == -300);
    meter.observe(SpecificForce{-300, 1000, 0}, 1200);
    CHECK(meter.now().lateral_mg == 300);
}

// Longitudinal is the flight-test quantity: positive is accelerating up the runway.
TEST_CASE("gload: longitudinal is the acceleration, positive forward") {
    flight::GMeter meter;
    meter.observe(SpecificForce{0, 1000, -400}, 1000);
    CHECK(meter.now().longitudinal_mg == 400);
    meter.observe(SpecificForce{0, 1000, 400}, 1200);
    CHECK(meter.now().longitudinal_mg == -400);
}

TEST_CASE("gload: the most and the least of each axis are held, and the first sample is both" *
          doctest::test_suite("main")) {
    flight::GMeter meter;
    meter.observe(SpecificForce{0, 1200, 0}, 1000);
    CHECK(meter.most().normal_mg == 1200);
    CHECK(meter.least().normal_mg == 1200);

    meter.observe(SpecificForce{-200, 4200, 0}, 1200);
    meter.observe(SpecificForce{300, -1300, 0}, 1400);
    meter.observe(SpecificForce{0, 1000, 0}, 1600);

    CHECK(meter.most().normal_mg == 4200);
    CHECK(meter.least().normal_mg == -1300);
    CHECK(meter.most().lateral_mg == 200);
    CHECK(meter.least().lateral_mg == -300);
    CHECK(meter.now().normal_mg == 1000);
}

// The peaks are this flight's, so the takeoff clears what the flight bag did.
TEST_CASE("gload: a reset leaves the peaks where the aircraft is now") {
    flight::GMeter meter;
    meter.observe(SpecificForce{0, 3000, 0}, 1000);
    meter.observe(SpecificForce{0, 1000, 0}, 1200);
    REQUIRE(meter.most().normal_mg == 3000);

    meter.reset();

    CHECK(meter.most().normal_mg == 1000);
    CHECK(meter.least().normal_mg == 1000);
}
