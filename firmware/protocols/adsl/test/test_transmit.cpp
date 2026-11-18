// The transmit rules ADS-L sets: G.1.16's rate, the two M-band channels and §C.5's slots.
#include <cstdint>
#include <initializer_list>

#include "doctest/doctest.h"
#include "protocols/adsl/flight_state.h"
#include "protocols/adsl/physical.h"
#include "protocols/adsl/transmit.h"

using namespace skyblip;
using namespace skyblip::adsl;

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
