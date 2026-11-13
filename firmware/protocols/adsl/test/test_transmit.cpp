// The transmit rules ADS-L sets: G.1.2's flight state, G.1.16's rate, the two M-band channels and
// §C.5's slots.
#include <cstdint>
#include <initializer_list>

#include "doctest/doctest.h"
#include "protocols/adsl/flight_state.h"
#include "protocols/adsl/physical.h"
#include "protocols/adsl/transmit.h"

using namespace skyblip;
using namespace skyblip::adsl;

// A paraglider soaring a ridge at walking pace went on air landed, once every ten seconds.
TEST_CASE("adsl flight state: a craft that can hover is never announced landed, only undefined") {
    const uint8_t on_ground = static_cast<uint8_t>(FlightState::Ground);
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

TEST_CASE("adsl transmit: the report period is 1 s airborne and 10 s on the ground") {
    CHECK(period_s(true) == 1u);
    CHECK(period_s(false) == kGroundReportPeriodS);
    CHECK(channel_hz(0) == kMBand0Hz);
    CHECK(channel_hz(1) == kMBand1Hz);
}

TEST_CASE("adsl transmit: the second is the ground stations' to 450 ms and the aircraft's after") {
    CHECK(kUplinkSlotStartMs == 200);
    CHECK(kUplinkSlotEndMs == kDirectStartMs);
    CHECK(kDirectStartMs == 450);
    CHECK(kDirectEndMs == 1000);
    CHECK(kAirTimeMs * 1000 >= 4640u);
}
