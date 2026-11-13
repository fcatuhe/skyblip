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

namespace {
constexpr int kFirstMs = kDirectStartMs;
constexpr int kLastMs = kDirectEndMs - 1 - static_cast<int>(kAirTimeMs);
}  // namespace

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
    for (uint32_t address : {0x000001u, 0x2ABCDEu, 0xDD1234u}) {
        int airborne = 0;
        int ground = 0;
        for (uint32_t utc_s = 1700000000; utc_s < 1700000100; utc_s++) {
            airborne += on_schedule(address, utc_s, true) ? 1 : 0;
            if (on_schedule(address, utc_s, false)) {
                ground++;
                CHECK(utc_s % kGroundReportPeriodS == ground_second(address));
            }
        }
        CHECK(airborne == 100);
        CHECK(ground == 10);
    }
}

TEST_CASE("adsl transmit: consecutive Traffic and Status transmissions change channel") {
    for (const bool airborne : {true, false}) {
        const uint32_t period = period_s(airborne);
        for (uint32_t utc_s = 1700000000; utc_s < 1700000100; utc_s += period)
            CHECK(channel_in(utc_s, airborne) != channel_in(utc_s + period, airborne));
    }
    for (uint32_t utc_s = 1700000000; utc_s < 1700000200; utc_s += kStatusPeriodS)
        CHECK(status_channel_in(utc_s) != status_channel_in(utc_s + kStatusPeriodS));
    CHECK(channel_hz(0) == kMBand0Hz);
    CHECK(channel_hz(1) == kMBand1Hz);
}

TEST_CASE("adsl transmit: a Status is due once every twenty seconds, in the ground second") {
    for (uint32_t address : {0x000001u, 0x2ABCDEu, 0xDD1234u}) {
        int due = 0;
        for (uint32_t utc_s = 1700000000; utc_s < 1700000200; utc_s++) {
            if (!status_due(address, utc_s)) continue;
            due++;
            CHECK(on_schedule(address, utc_s, false));
        }
        CHECK(due == 10);
    }
}

TEST_CASE("adsl transmit: an instant is drawn inside its window, fresh each second and address") {
    int moved = 0;
    int apart = 0;
    for (uint32_t utc_s = 1700000000; utc_s < 1700000500; utc_s++) {
        const int instant = instant_between(0x2ABCDE, utc_s, kFirstMs, kLastMs);
        CHECK(instant >= kFirstMs);
        CHECK(instant <= kLastMs);
        moved += instant != instant_between(0x2ABCDE, utc_s + 1, kFirstMs, kLastMs) ? 1 : 0;
        apart += instant != instant_between(0x2ABCDF, utc_s, kFirstMs, kLastMs) ? 1 : 0;
        CHECK(instant == instant_between(0x2ABCDE, utc_s, kFirstMs, kLastMs));
    }
    CHECK(moved > 490);
    CHECK(apart > 490);
    CHECK(instant_between(0x2ABCDE, 1700000000, 700, 700) == 700);
}

TEST_CASE("adsl transmit: a Status instant never starts within apart of the Traffic instant") {
    constexpr int kApartMs = static_cast<int>(kAirTimeMs) + 2;
    for (uint32_t address = 1; address < 1025; address++) {
        const uint32_t utc_s = 1700000000 + address;
        const int traffic = instant_between(address, utc_s, kFirstMs, kLastMs);
        const int status = status_instant_between(address, utc_s, kFirstMs, kLastMs, kApartMs);
        CHECK(status >= kFirstMs);
        CHECK(status <= kLastMs);
        CHECK((status <= traffic - kApartMs || status >= traffic + kApartMs));
    }
}
