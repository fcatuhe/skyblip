// ADS-L 4 SRD-860 issue 2 Subpart E, the network layer: the version own-ship's packets carry.
#include <cstdint>
#include <initializer_list>

#include "core/model/ownship.h"
#include "core/protocol/adsl.h"
#include "core/radio/log.h"
#include "doctest/doctest.h"
#include "test/support/m_band_rig.h"
#include "test/support/product_rig.h"

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

// A neighbour 100 m north of own-ship, sent under the network header given, as it reaches the chip.
void hear_with_header(Rig& rig, uint8_t header) {
    model::Ownship neighbour = rig.state().own;
    neighbour.latitude_1e7 += 10000;
    adsl::Packet packet{};
    REQUIRE(protocol::from_own(packet, neighbour, 0xC5D804, 6, 4));
    packet.version = header;
    if (((header >> 5) & 0x03) == 0) packet.scramble();
    packet.set_crc();
    hear_m_band(rig, adsl::kSyncWord, packet.data, adsl::kFrameBytes);
}

uint8_t version_sent(const model::Ownship& own, uint8_t category = 4) {
    adsl::Packet packet{};
    REQUIRE(protocol::from_own(packet, own, 0x123456, 6, category));
    return packet.protocol_version();
}
}  // namespace

// What own-ship knows decides the codes, and the codes decide the issue a receiver must read.
TEST_CASE(
    "ADS-L.4.SRD860.E.1.1: own-ship sends version 0 with all it knows, 1 once it marks a field "
    "absent" *
    doctest::test_suite("main")) {
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
}

// "Low-power or constrained ADS-L clients may optionally use key index 3", and the Go draws them.
TEST_CASE("ADS-L.4.SRD860.E.1.3: a neighbour sending under key 3 is heard and drawn" *
          doctest::test_suite("main")) {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    fly(rig, now_ms, 3);
    hear_with_header(rig, 0x60);
    settle(rig, now_ms);
    CHECK(heard_verdict(rig) == radio::Verdict::Received);
    CHECK(rig.state().traffic.count() == 1);
}

// A reserved key, a later issue or the FEC bit names a packet this device cannot read as Issue 2.
TEST_CASE("ADS-L.4.SRD860.E.1.1: a frame the Go cannot read as Issue 2 is counted, never drawn") {
    for (const uint8_t header : {uint8_t{0x20}, uint8_t{0x40}, uint8_t{0x02}, uint8_t{0x80}}) {
        CAPTURE(int(header));
        Rig rig;
        REQUIRE(rig.setup() == Status::Ok);
        uint32_t now_ms = 0;
        fly(rig, now_ms, 3);
        const uint32_t bad_before = rig.state().air.rx_bad;
        hear_with_header(rig, header);
        settle(rig, now_ms);
        CHECK(heard_verdict(rig) == radio::Verdict::Unsupported);
        CHECK(rig.state().air.rx_type == 1);
        CHECK(rig.state().air.rx_bad == bad_before);
        CHECK(rig.state().traffic.count() == 0);
    }
}
