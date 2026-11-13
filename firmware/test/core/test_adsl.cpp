// Own-ship as the ADS-L frame skyBlip transmits: what it does not know, its address and its
// integrity claim.
#include <cstdlib>  // std::abs - libc++ pulls it in transitively, libstdc++ does not
#include <cstring>
#include <initializer_list>  // same: a braced list in a range-for needs it named
#include <utility>

#include "core/model/ownship.h"
#include "core/protocol/adsl.h"
#include "core/protocol/callsign.h"
#include "core/settings/address.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::protocol;

TEST_CASE("adsl: from_own marks what own-ship does not know") {
    skyblip::model::Ownship own{};
    own.latitude_1e7 = 485000000;
    own.longitude_1e7 = 85000000;
    own.altitude_hae_mm = 1500000;  // HAE: what G.1.7 transmits
    own.speed_mm_s = 50000;
    own.climb_mm_s = 2000;

    // No fix yet: altitude and ground speed are unavailable, not zero.
    adsl::Packet packet{};
    from_own(packet, own, 0xABCDEF, 6, 4);
    CHECK_FALSE(packet.has_altitude());
    CHECK_FALSE(packet.has_speed());
    CHECK_FALSE(packet.has_climb());  // climb_valid is false too

    // A 3D fix, but no vertical rate derived yet: only the climb stays unavailable.
    own.fix_valid = true;
    own.vdop_e2 = 150;
    from_own(packet, own, 0xABCDEF, 6, 4);
    CHECK(packet.has_altitude());
    CHECK(packet.altitude_m() == 1500);
    CHECK(packet.has_speed());
    CHECK_FALSE(packet.has_climb());

    // Everything known.
    own.climb_valid = true;
    from_own(packet, own, 0xABCDEF, 6, 4);
    CHECK(packet.has_climb());
    CHECK(packet.climb_e8() == 16);
}

TEST_CASE("adsl: an address goes out as it was handed, in whichever table") {
    skyblip::model::Ownship own{};
    own.fix_valid = true;

    for (uint8_t table : {uint8_t(0), uint8_t(5), uint8_t(6), uint8_t(7), uint8_t(9)}) {
        adsl::Packet packet{};
        CHECK(from_own(packet, own, 0xDD1234u, table, 4));
        CHECK(packet.address() == 0xDD1234u);
        CHECK(packet.address_mapping_table() == table);
    }
}

// Issue #126: the third of three refusals, so a caller past the other two still sends nothing.
TEST_CASE("adsl: without an address the encoder builds no frame, position or name") {
    skyblip::model::Ownship own{};
    own.fix_valid = true;

    adsl::Packet untouched{};
    untouched.reset();
    const adsl::Packet before = untouched;
    CHECK_FALSE(from_own(untouched, own, std::nullopt, 58, 4));
    CHECK_FALSE(from_own_callsign(untouched, std::nullopt, 58, "D-KXYZ"));
    CHECK(std::memcmp(&untouched, &before, sizeof(adsl::Packet)) == 0);
}

// Every field of the Integrity block has a zero code meaning "unknown / no fix"
// (G.1.11 to G.1.16), so an untouched block is not neutral: it tells a receiver
// we claim no integrity at all, and receivers are entitled to weight us
// accordingly. Without a fix that is the truthful answer; with one it is not.
TEST_CASE("adsl: from_own claims integrity from the receiver's DOP") {
    skyblip::model::Ownship own{};
    own.latitude_1e7 = 485000000;
    own.longitude_1e7 = 85000000;
    own.altitude_hae_mm = 1500000;
    own.speed_mm_s = 50000;
    own.hdop_e2 = 90;
    own.vdop_e2 = 150;

    adsl::Packet packet{};
    from_own(packet, own, 0xABCDEF, 6, 4);
    CHECK(int(packet.source_integrity) == 0);
    CHECK(int(packet.navigation_integrity) == 0);
    CHECK(int(packet.horizontal_accuracy) == 0);
    CHECK(int(packet.vertical_accuracy) == 0);
    CHECK(int(packet.velocity_accuracy) == 0);

    // HDOP 0.9 and VDOP 1.5 with a fix: 10.8 m horizontal and 18 m vertical, 2 * DOP * 6 m.
    own.fix_valid = true;
    from_own(packet, own, 0xABCDEF, 6, 4);
    CHECK(int(packet.source_integrity) == 0);
    CHECK(int(packet.design_assurance) == adsl::Packet::kDesignAssuranceNone);
    CHECK(int(packet.navigation_integrity) == 0);
    CHECK(int(packet.horizontal_accuracy) == 5);  // HFOM < 30 m
    CHECK(int(packet.vertical_accuracy) == 2);    // VFOM < 45 m
    CHECK(int(packet.velocity_accuracy) == 1);    // < 10 m/s

    // A 2D solution reports no VDOP, and a height nothing measured the quality
    // of gets no claim at all: the horizontal half of the block still stands.
    own.vdop_e2 = 0;
    from_own(packet, own, 0xABCDEF, 6, 4);
    CHECK(int(packet.horizontal_accuracy) == 5);
    CHECK(int(packet.vertical_accuracy) == 0);

    // A receiver that reports no HDOP is not a receiver reporting a good one.
    own.hdop_e2 = 0;
    from_own(packet, own, 0xABCDEF, 6, 4);
    CHECK(int(packet.source_integrity) == 0);
    CHECK(int(packet.horizontal_accuracy) == 0);
}

// A degrading fix walks the codes down together, and a hopeless one claims
// nothing rather than claiming a number nobody should act on.
TEST_CASE("adsl: a degrading HDOP walks the accuracy claim down") {
    skyblip::model::Ownship own{};
    own.fix_valid = true;
    adsl::Packet packet{};

    own.hdop_e2 = 200;  // 24 m horizontal
    own.vdop_e2 = 200;  // 24 m vertical
    from_own(packet, own, 1, 6, 4);
    CHECK(int(packet.horizontal_accuracy) == 5);
    CHECK(int(packet.vertical_accuracy) == 2);
    CHECK(int(packet.velocity_accuracy) == 1);

    own.hdop_e2 = 800;  // 96 m horizontal
    own.vdop_e2 = 800;  // 96 m vertical
    from_own(packet, own, 1, 6, 4);
    CHECK(int(packet.horizontal_accuracy) == 3);  // 0.05 to 0.1 NM
    CHECK(int(packet.vertical_accuracy) == 1);
    CHECK(int(packet.velocity_accuracy) == 0);

    own.hdop_e2 = 9999;  // 1200 m: beyond 0.5 NM, the same code as no fix
    own.vdop_e2 = 9999;
    from_own(packet, own, 1, 6, 4);
    CHECK(int(packet.horizontal_accuracy) == 0);
    CHECK(int(packet.vertical_accuracy) == 0);  // beyond 150 m: no claim at all
    CHECK(int(packet.velocity_accuracy) == 0);
}

// A codec that agrees with itself can still put a frame on air no other receiver reads.
TEST_CASE("adsl: a burst is byte for byte the frame other receivers already read") {
    skyblip::model::Ownship own{};
    own.fix_valid = true;
    own.utc_valid = true;
    own.climb_valid = true;
    own.utc_s = 1700000007;
    own.latitude_1e7 = -337654321;
    own.longitude_1e7 = 1512345678;
    own.altitude_hae_mm = 2345678;
    own.speed_mm_s = 61000;
    own.climb_mm_s = -3200;
    own.track_cdeg = 27350;
    own.hdop_e2 = 90;
    own.vdop_e2 = 140;
    adsl::Packet packet{};
    REQUIRE(from_own(packet, own, 0x2ABCDE, 58, 1));
    const uint8_t clear[] = {0x00, 0x02, 0xBA, 0x37, 0xAF, 0x0A, 0x30, 0x21,
                             0x7B, 0xFA, 0xCF, 0x31, 0x8B, 0x6B, 0x8D, 0x6A,
                             0x8A, 0xC6, 0xC2, 0x00, 0x35, 0x00, 0x00, 0x00};
    CHECK(std::memcmp(packet.data, clear, sizeof(clear)) == 0);

    packet.scramble();
    packet.set_crc();
    const uint8_t air[] = {0x00, 0x5E, 0x6F, 0xC1, 0xDB, 0x84, 0x85, 0x87, 0xBC, 0xBF, 0x23, 0x61,
                           0x44, 0x47, 0x51, 0xC5, 0xA3, 0x07, 0x3D, 0xE0, 0x15, 0x22, 0x93, 0x11};
    CHECK(std::memcmp(packet.data, air, sizeof(air)) == 0);

    adsl::Packet callsign{};
    REQUIRE(from_own_callsign(callsign, 0x2ABCDE, 58, "F-JXYZ"));
    callsign.scramble();
    callsign.set_crc();
    const uint8_t callsign_air[] = {0x00, 0x05, 0xE8, 0x01, 0x68, 0xD4, 0x33, 0x25,
                                    0x91, 0xD0, 0x2C, 0x55, 0xAF, 0x9E, 0x73, 0x56,
                                    0xE1, 0x0A, 0x2A, 0x36, 0xE7, 0xF2, 0xCC, 0x88};
    CHECK(std::memcmp(callsign.data, callsign_air, sizeof(callsign_air)) == 0);
}
