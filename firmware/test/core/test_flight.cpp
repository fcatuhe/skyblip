// Airborne or not, from the fix stream: it gates the DFU lockout and the transmit rate, never a
// page.
#include <initializer_list>

#include "core/flight/ground.h"
#include "core/flight/state.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::flight;

namespace {

FlightSample solution(double mps, uint16_t hdop_e2 = 90) {
    FlightSample s{};
    s.speed_mm_s = static_cast<int32_t>(mps * 1000);
    s.hdop_e2 = hdop_e2;
    s.fix_valid = true;
    return s;
}

// One second of solutions at a time, the cadence the receiver is configured for.
struct Stream {
    FlightMonitor monitor;
    uint32_t now_ms{0};

    FlightState hold(int seconds, double mps, uint16_t hdop_e2 = 90) {
        FlightState state = monitor.state();
        for (int i = 0; i < seconds; i++) state = one(mps, hdop_e2);
        return state;
    }

    FlightState one(double mps, uint16_t hdop_e2 = 90) {
        now_ms += 1000;
        return monitor.update(solution(mps, hdop_e2), now_ms);
    }

    FlightState blind(int seconds = 1) {
        FlightState state = monitor.state();
        for (int i = 0; i < seconds; i++) {
            now_ms += 1000;
            state = monitor.update(FlightSample{}, now_ms);
        }
        return state;
    }

    FlightState state() const { return monitor.state(); }
    bool rolling() const { return monitor.rolling(); }
};

}  // namespace

// 23 kt is a speed no aircraft taxis at and every takeoff roll passes it well before it flies.
// A paraglider soaring a ridge at walking pace went on air landed, once every ten seconds.
TEST_CASE("flight: a craft that can hover is never announced landed, only undefined") {
    const uint8_t on_ground = static_cast<uint8_t>(FlightState::OnGround);
    const uint8_t airborne_code = static_cast<uint8_t>(FlightState::Airborne);
    const uint8_t undefined = static_cast<uint8_t>(FlightState::Unknown);
    for (const uint8_t hovers : {3, 5, 7, 8, 9, 10, 11, 12, 13, 15, 16, 17}) {
        CAPTURE(int(hovers));
        CHECK(announced_state(on_ground, hovers) == undefined);
        CHECK_FALSE(reduced_rate(announced_state(on_ground, hovers)));
        CHECK(announced_state(airborne_code, hovers) == airborne_code);
    }
    for (const uint8_t fixed_wing : {0, 1, 2, 4, 6, 14}) {
        CAPTURE(int(fixed_wing));
        CHECK(announced_state(on_ground, fixed_wing) == on_ground);
        CHECK(reduced_rate(announced_state(on_ground, fixed_wing)));
    }
}

TEST_CASE("flight: a takeoff is the speed no taxi holds, and it waits for nothing") {
    Stream sky;
    REQUIRE(sky.hold(3, 0.0) == FlightState::OnGround);

    CHECK(sky.hold(1, 6.0) == FlightState::OnGround);   // a brisk taxi
    CHECK(sky.hold(1, 11.0) == FlightState::OnGround);  // the roll, accelerating
    CHECK(sky.hold(1, 12.0) == FlightState::Airborne);
}

// A glider towed to the grid, a tug taxiing back, a trailer on the perimeter track.
TEST_CASE("flight: a taxi does not take off, and one bad solution does not either") {
    Stream sky;
    REQUIRE(sky.hold(3, 0.0) == FlightState::OnGround);
    // A tug hurrying back to the grid does 10 m/s, and it is what this threshold is set over.
    CHECK(sky.hold(30, 10.0) == FlightState::OnGround);

    CHECK(sky.one(45.0) == FlightState::OnGround);
    CHECK(sky.hold(20, 10.0) == FlightState::OnGround);
}

// A stop is a landing on the solution that shows it, with no hold to wait out.
TEST_CASE("flight: a landing is a standstill, and a fast rollout is not one yet") {
    Stream sky;
    REQUIRE(sky.hold(3, 25.0) == FlightState::Airborne);

    CHECK(sky.hold(1, 20.0) == FlightState::Airborne);
    CHECK(sky.hold(1, 8.0) == FlightState::Airborne);
    CHECK(sky.hold(1, 2.0) == FlightState::Airborne);
    CHECK(sky.hold(1, 0.5) == FlightState::OnGround);
}

// The other landing: a tug that rolls in and taxis back has landed, and the glass says TAXI.
TEST_CASE("flight: a taxi held for the landing hold is a landing, stop or no stop") {
    Stream sky;
    REQUIRE(sky.hold(3, 25.0) == FlightState::Airborne);

    CHECK(sky.hold(10, 5.0) == FlightState::Airborne);  // the hold runs from the first slow fix
    CHECK(sky.hold(1, 5.0) == FlightState::OnGround);
    // Landed while still rolling, which is the pair the word TAXI is drawn from.
    CHECK(sky.rolling());

    CHECK(sky.hold(60, 6.0) == FlightState::OnGround);
    CHECK(sky.rolling());
}

// A rollout that never slows below a taxi is a touch and go, not a landing.
TEST_CASE("flight: a fast roll through the airfield does not land the aircraft") {
    Stream sky;
    REQUIRE(sky.hold(3, 25.0) == FlightState::Airborne);

    CHECK(sky.hold(120, 10.0) == FlightState::Airborne);
    CHECK(sky.hold(1, 25.0) == FlightState::Airborne);
}

// A thermalling glider in a strong wind is slow upwind every circle and lands on none of them.
TEST_CASE("flight: a circle that dips below the taxi ceiling is not a landing") {
    Stream sky;
    REQUIRE(sky.hold(3, 25.0) == FlightState::Airborne);

    for (int circle = 0; circle < 5; circle++) {
        CHECK(sky.hold(9, 5.0) == FlightState::Airborne);
        CHECK(sky.hold(16, 35.0) == FlightState::Airborne);
    }
}

// The hold is evidence, and an antenna that drops out reports none.
TEST_CASE("flight: an outage does not count towards the landing hold") {
    Stream sky;
    REQUIRE(sky.hold(3, 25.0) == FlightState::Airborne);

    CHECK(sky.hold(10, 4.0) == FlightState::Airborne);
    CHECK(sky.blind(30) == FlightState::Unknown);
    CHECK(sky.hold(10, 4.0) == FlightState::Airborne);
    CHECK(sky.hold(1, 4.0) == FlightState::OnGround);
}

// Evidence is worth what the fix behind it is worth, so OGN divides by any DOP above 1.0.
TEST_CASE("flight: a fix nobody should trust does not take off on its own") {
    Stream sky;
    REQUIRE(sky.hold(3, 0.0, 3000) == FlightState::OnGround);
    CHECK(sky.hold(20, 12.0, 3000) == FlightState::OnGround);

    // The same movement on a fix worth trusting is a takeoff.
    CHECK(sky.hold(1, 12.0, 90) == FlightState::Airborne);
}

// The derating cuts one way: a poor sky may refuse a takeoff, never declare a landing.
TEST_CASE("flight: the dilution of precision cannot land an aircraft") {
    CHECK_FALSE(flight_evidence(solution(12.0, 3000)));
    CHECK_FALSE(ground_evidence(solution(12.0, 3000)));
    CHECK(flight_evidence(solution(12.0, 90)));

    CHECK_FALSE(flight_evidence(solution(11.9)));
    CHECK(ground_evidence(solution(0.9)));
    CHECK_FALSE(ground_evidence(solution(1.2)));
    // A receiver that reports no DOP at all is not punished for it.
    CHECK(flight_evidence(solution(12.0, 0)));
}

// The third band: stopped or moving, which is the word the glass prints while ADS-L says OnGround.
TEST_CASE("flight: a parked receiver's noise is not a taxi, and a slowing taxi still is") {
    Stream sky;
    REQUIRE(sky.hold(3, 0.2) == FlightState::OnGround);
    CHECK_FALSE(sky.rolling());

    // A metre a second of multipath on a device nobody has touched.
    CHECK_FALSE(sky.hold(5, 1.25) == FlightState::Airborne);
    CHECK_FALSE(sky.rolling());

    sky.one(2.0);  // pushed to the grid at a brisk walk
    CHECK(sky.rolling());

    sky.hold(5, 1.25);  // slowing for the turn, and the word holds
    CHECK(sky.rolling());

    sky.hold(2, 0.75);
    CHECK_FALSE(sky.rolling());
}

// A taxi does not stop because the antenna did.
TEST_CASE("flight: an outage leaves the aircraft rolling as it was") {
    Stream sky;
    REQUIRE(sky.one(6.0) == FlightState::OnGround);
    REQUIRE(sky.rolling());

    CHECK(sky.blind() == FlightState::Unknown);
    CHECK(sky.rolling());
}

// The first solution decides on its own evidence: a device rebooted in flight must not wait.
TEST_CASE("flight: a device switched on in the air says so at once") {
    Stream airborne;
    CHECK(airborne.one(30.0) == FlightState::Airborne);

    // Switched on while being towed to the grid, which is the common half of this.
    Stream rolling;
    CHECK(rolling.one(6.0) == FlightState::OnGround);

    Stream parked;
    CHECK(parked.one(0.4) == FlightState::OnGround);
}

// Without a fix there is no claim to make, and the aircraft is still where it was.
TEST_CASE("flight: no fix is not a landing") {
    Stream sky;
    REQUIRE(sky.hold(3, 30.0) == FlightState::Airborne);

    CHECK(sky.blind() == FlightState::Unknown);
    CHECK(sky.state() == FlightState::Airborne);

    CHECK(sky.one(30.0) == FlightState::Airborne);
}

// The jerk gate compares two consecutive solutions, so an outage must not arm it.
TEST_CASE("flight: the first solution after an outage is not judged as a jerk") {
    Stream sky;
    REQUIRE(sky.hold(3, 0.0) == FlightState::OnGround);

    REQUIRE(sky.blind() == FlightState::Unknown);
    CHECK(sky.one(30.0) == FlightState::Airborne);
}

TEST_CASE("flight: only the two ADS-L G.1.2 codes name a state, every other value is unknown") {
    CHECK(state_from(static_cast<uint8_t>(FlightState::OnGround)) == FlightState::OnGround);
    CHECK(state_from(static_cast<uint8_t>(FlightState::Airborne)) == FlightState::Airborne);
    CHECK(state_from(static_cast<uint8_t>(FlightState::Unknown)) == FlightState::Unknown);
    CHECK(airborne(static_cast<uint8_t>(FlightState::Airborne)));
    CHECK_FALSE(airborne(static_cast<uint8_t>(FlightState::OnGround)));

    // Two bits, and we own neither the sender nor the future: an unknown code unlocks nothing.
    for (uint16_t code = 3; code < 256; code++)
        CHECK(state_from(static_cast<uint8_t>(code)) == FlightState::Unknown);
}

TEST_CASE("flight: a lost fix is not a landing, so the ground latch holds airborne") {
    GroundLatch latch;
    CHECK(latch.state() == FlightState::Unknown);
    CHECK_FALSE(latch.on_ground());

    latch.update(FlightState::OnGround);
    CHECK(latch.on_ground());

    // Unknown before anything was confirmed is not a ground: every gate behind this fails closed.
    latch.update(FlightState::Unknown);
    CHECK(latch.state() == FlightState::Unknown);
    CHECK_FALSE(latch.on_ground());

    latch.update(FlightState::Airborne);
    latch.update(FlightState::Unknown);
    CHECK(latch.state() == FlightState::Airborne);

    latch.update(FlightState::OnGround);
    CHECK(latch.on_ground());
}
