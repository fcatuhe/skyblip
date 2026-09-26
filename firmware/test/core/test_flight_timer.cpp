// The clock a pilot copies into a logbook, read once stopped: a landing must not clear it.
#include "core/flight/timer.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::flight;

namespace {
constexpr uint32_t kMinute = 60000;
}

TEST_CASE("flight timer: nothing has flown until something takes off") {
    FlightTimer timer;
    CHECK_FALSE(timer.flown());

    timer.update(FlightState::Ground, 10000);
    CHECK_FALSE(timer.flown());

    timer.update(FlightState::Airborne, 20000);
    CHECK(timer.flown());
    CHECK(timer.seconds() == 0);
}

TEST_CASE("flight timer: the count runs from the takeoff, not from the boot") {
    FlightTimer timer;
    timer.update(FlightState::Ground, 5 * kMinute);
    timer.update(FlightState::Airborne, 8 * kMinute);
    timer.update(FlightState::Airborne, 15 * kMinute);
    CHECK(timer.seconds() == 7 * 60);
}

// A touch and go is one hour in the logbook, not two entries the pilot has to add up.
TEST_CASE("flight timer: a landing holds the figure and the next takeoff carries on from it") {
    FlightTimer timer;
    timer.update(FlightState::Airborne, 0);
    timer.update(FlightState::Airborne, 42 * kMinute);
    REQUIRE(timer.seconds() == 42 * 60);

    // An hour on the ground adds nothing, and does not take anything away either.
    timer.update(FlightState::Ground, 43 * kMinute);
    timer.update(FlightState::Ground, 100 * kMinute);
    CHECK(timer.seconds() == 42 * 60);
    CHECK(timer.flown());
    CHECK_FALSE(timer.running());

    timer.update(FlightState::Airborne, 100 * kMinute);
    CHECK(timer.seconds() == 42 * 60);
    CHECK(timer.running());
    timer.update(FlightState::Airborne, 103 * kMinute);
    CHECK(timer.seconds() == 45 * 60);
}

// A minute under a wing is a minute flown: it neither lands the aircraft nor stops the clock.
TEST_CASE("flight timer: an outage is not a landing, and the clock runs across it") {
    FlightTimer timer;
    timer.update(FlightState::Airborne, 0);
    timer.update(FlightState::Unknown, 5 * kMinute);
    CHECK(timer.running());
    CHECK(timer.seconds() == 5 * 60);

    timer.update(FlightState::Airborne, 10 * kMinute);
    CHECK(timer.seconds() == 10 * 60);
}

TEST_CASE("flight timer: a receiver that has never solved starts no flight") {
    FlightTimer timer;
    timer.update(FlightState::Unknown, 5 * kMinute);
    timer.update(FlightState::Unknown, 40 * kMinute);
    CHECK_FALSE(timer.flown());
    CHECK_FALSE(timer.running());
    CHECK(timer.seconds() == 0);
}

TEST_CASE("flight timer: the count crosses the 49.7-day wrap") {
    FlightTimer timer;
    const uint32_t takeoff = 0xFFFFFFFFu - kMinute;
    timer.update(FlightState::Airborne, takeoff);
    timer.update(FlightState::Airborne, takeoff + 3 * kMinute);
    CHECK(timer.seconds() == 3 * 60);
}
