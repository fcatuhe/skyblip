// The transmit rules ADS-L sets: G.1.2's flight state, G.1.16's rate, §C.2.5's channel alternation,
// G.2.11's Status cadence and the instants drawn inside §C.5's direct slot.
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

TEST_CASE("adsl transmit: Traffic goes out every second airborne, and once in ten on the ground") {
    for (uint32_t ground_second = 0; ground_second < kGroundReportPeriodS; ground_second++) {
        int airborne = 0;
        int ground = 0;
        for (uint32_t utc_s = 1700000000; utc_s < 1700000100; utc_s++) {
            airborne += on_schedule(utc_s, true, ground_second) ? 1 : 0;
            if (on_schedule(utc_s, false, ground_second)) {
                ground++;
                CHECK(utc_s % kGroundReportPeriodS == ground_second);
            }
        }
        CHECK(airborne == 100);
        CHECK(ground == 10);
    }
}

TEST_CASE("adsl transmit: consecutive Traffic transmissions change channel") {
    for (const bool airborne : {true, false}) {
        const uint32_t period = period_s(airborne);
        for (uint32_t utc_s = 1700000000; utc_s < 1700000100; utc_s += period)
            CHECK(channel_in(utc_s, airborne) != channel_in(utc_s + period, airborne));
    }
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
