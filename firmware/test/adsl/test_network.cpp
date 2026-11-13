// ADS-L 4 SRD-860 issue 2 Subpart E, the network layer: the version own-ship's packets carry.
#include <cstdint>

#include "core/model/ownship.h"
#include "core/protocol/adsl.h"
#include "core/protocol/callsign.h"
#include "doctest/doctest.h"

using namespace skyblip;

namespace {
model::Ownship flying() {
    model::Ownship own{};
    own.fix_valid = true;
    own.utc_valid = true;
    own.climb_valid = true;
    own.latitude_1e7 = 481234500;
    own.longitude_1e7 = 81234500;
    own.altitude_hae_mm = 1000000;
    own.speed_mm_s = 40000;
    own.hdop_e2 = 90;
    own.vdop_e2 = 150;
    own.flight_state = 2;
    return own;
}

uint8_t version_sent(const model::Ownship& own, uint8_t category = 4) {
    adsl::Packet packet{};
    REQUIRE(protocol::from_own(packet, own, 0x123456, 6, category));
    return packet.protocol_version();
}
}  // namespace

// What own-ship knows decides the codes, and the codes decide the issue a receiver must read.
TEST_CASE("ADS-L.4.SRD860.E.1.1: own-ship sends version 0 with all it knows, 1 once it marks a field absent") {
    CHECK(version_sent(flying()) == 0);

    model::Ownship two_d = flying();
    two_d.vdop_e2 = 0;
    CHECK(version_sent(two_d) == 1);

    model::Ownship no_climb = flying();
    no_climb.climb_valid = false;
    CHECK(version_sent(no_climb) == 1);

    model::Ownship no_fix = flying();
    no_fix.fix_valid = false;
    CHECK(version_sent(no_fix) == 1);

    CHECK(version_sent(flying(), 15) == 1);

    adsl::Packet name{};
    REQUIRE(protocol::from_own_callsign(name, 0x123456, 58, "D-KXYZ"));
    CHECK(name.protocol_version() == 1);
}
