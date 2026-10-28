// The ADS-L position packet end to end, a radio protocol checked with no RF
// hardware in the room: the channel is an injectable bit error rate, and the
// packet has to survive it or say that it did not. What these separate is a
// *detected failure*, a frame dropped and counted, from a *silent
// miscorrection*, a frame that decodes to a plausible aircraft somewhere it is
// not. Only the second one collides with anybody.
#include <cstdlib>  // std::abs - libc++ pulls it in transitively, libstdc++ does not
#include <cstring>
#include <initializer_list>  // same: a braced list in a range-for needs it named

#include "core/model/ownship.h"
#include "core/protocol/adsl.h"
#include "core/settings/address.h"
#include "doctest/doctest.h"
#include "test/support/random.h"

using namespace skyblip::protocol;

namespace {
AdslPacket make_reference() {
    AdslPacket packet;
    packet.reset();
    packet.set_address(0x123456);
    packet.set_address_mapping_table(6);  // FLARM/ALP-TAS table
    packet.timestamp = 4 * 4;             // 4.0 s
    packet.flight_state = 2;              // airborne
    packet.aircraft_category = 4;         // glider
    packet.emergency = 1;
    packet.set_latitude_1e7(481234500);  // 48.12345 deg
    packet.set_longitude_1e7(81234500);  //  8.12345 deg
    packet.set_altitude_m(1234);
    packet.set_speed_q(45 * 4);
    packet.set_climb_e8(-44);  // -5.5 m/s
    packet.set_track_c9(256);  // 180 deg
    packet.source_integrity = 3;
    packet.design_assurance = 2;
    packet.navigation_integrity = 11;
    packet.horizontal_accuracy = 6;
    packet.vertical_accuracy = 3;
    packet.velocity_accuracy = 2;
    return packet;
}
}  // namespace

TEST_CASE("adsl: field accessors round-trip through the wire layout") {
    AdslPacket packet = make_reference();
    CHECK(packet.address() == 0x123456);
    CHECK(packet.address_mapping_table() == 6);
    CHECK(int(packet.timestamp) == 16);
    CHECK(int(packet.flight_state) == 2);
    CHECK(int(packet.aircraft_category) == 4);
    CHECK(std::abs(packet.latitude_1e7() - 481234500) < 200);
    CHECK(std::abs(packet.longitude_1e7() - 81234500) < 200);
    CHECK(std::abs(packet.altitude_m() - 1234) <= 8);
    CHECK(std::abs(int(packet.speed_q()) - 180) <= 4);
    CHECK(packet.has_climb());
    CHECK(std::abs(int(packet.climb_e8()) - (-44)) <= 8);
    CHECK(packet.track_c9() == 256);
}

TEST_CASE("adsl: scramble+crc, then descramble+check round-trip is lossless (BER 0)") {
    AdslPacket packet = make_reference();
    AdslPacket golden = packet;
    packet.scramble();
    packet.set_crc();
    // On air these 21 bytes ride. The RX recomputes.
    CHECK(packet.check_crc() == 0);
    packet.descramble();
    // payload identical to golden (compare the scrambled region bytes)
    CHECK(std::memcmp(packet.payload, golden.payload, 20) == 0);
    CHECK(packet.address() == golden.address());
    CHECK(packet.latitude_1e7() == golden.latitude_1e7());
}

TEST_CASE("adsl: single-bit error is corrected via CRC syndrome") {
    AdslPacket packet = make_reference();
    packet.scramble();
    packet.set_crc();
    uint8_t error[AdslPacket::kDataBytes] = {0};
    // flip a data bit (in the CRC-covered region)
    packet.data[7] ^= 0x08;
    CHECK(packet.check_crc() != 0);
    int corrected = packet.correct(error, 6);
    CHECK(corrected == 1);
    CHECK(packet.check_crc() == 0);
}

TEST_CASE("adsl: multi-bit errors within flagged (weak) bits are corrected") {
    AdslPacket packet = make_reference();
    packet.scramble();
    packet.set_crc();
    uint8_t* data = packet.data;
    uint8_t error[AdslPacket::kDataBytes] = {0};
    // flip 3 bits and mark them weak (as a Manchester decoder would)
    struct BitLocation {
        int byte;
        uint8_t mask;
    } locations[3] = {{3, 0x40}, {9, 0x02}, {15, 0x80}};
    for (auto& location : locations) {
        data[location.byte] ^= location.mask;
        error[location.byte] |= location.mask;
    }
    CHECK(packet.check_crc() != 0);
    int corrected = packet.correct(error, 6);
    CHECK(corrected >= 0);
    CHECK(packet.check_crc() == 0);
}

TEST_CASE("adsl: a flagged bit past the eighth is searched, not wrapped back onto the first") {
    AdslPacket packet = make_reference();
    packet.scramble();
    packet.set_crc();
    const AdslPacket sent = packet;
    uint8_t error[AdslPacket::kDataBytes] = {0};
    for (int byte = 2; byte < 12; byte++) error[byte] = 0x10;
    for (int byte : {9, 10, 11}) packet.data[byte] ^= 0x10;
    REQUIRE(packet.check_crc() != 0);

    CHECK(packet.correct(error, 16) == 3);
    CHECK(std::memcmp(packet.data, sent.data, AdslPacket::kDataBytes) == 0);
}

TEST_CASE("adsl: Monte-Carlo BER, detected vs silent miscorrection accounting") {
    // Push random-ish bit errors, some flagged (weak) and some not, and count:
    //   good      = decoded to the exact original codeword
    //   detected  = check_crc != 0 after correct() (safe: we know it failed)
    //   silent    = check_crc == 0 but payload != original (the DANGER mode)
    // We assert silent miscorrection stays rare and is *counted*, never hidden.
    skyblip::Random random(0xC0FFEE);
    const int kTrials = 20000;
    int good = 0, detected = 0, silent = 0;

    for (int trial = 0; trial < kTrials; trial++) {
        AdslPacket tx = make_reference();
        tx.set_address(0x100000 + (random.next() & 0xFFFFF));
        tx.scramble();
        tx.set_crc();

        AdslPacket rx = tx;
        uint8_t* data = rx.data;
        uint8_t error[AdslPacket::kDataBytes] = {0};

        // Inject up to 3 bit errors, and flag ~70% of them as weak.
        int errors = (random.next() % 4);  // 0..3
        for (int injected = 0; injected < errors; injected++) {
            int bit = random.next() % (AdslPacket::kDataBytes * 8);
            int byte = bit >> 3;
            uint8_t mask = static_cast<uint8_t>(1 << (7 - (bit & 7)));
            data[byte] ^= mask;
            if ((random.next() % 10) < 7) error[byte] |= mask;
        }

        rx.correct(error, 6);
        bool crc_ok = (rx.check_crc() == 0);
        bool same = (std::memcmp(rx.payload, tx.payload, 20) == 0) && rx.version == tx.version;

        if (crc_ok && same)
            good++;
        else if (!crc_ok)
            detected++;
        else
            silent++;  // crc_ok but wrong -> silent miscorrection
    }

    MESSAGE("good=" << good << " detected=" << detected << " silent=" << silent);
    CHECK(good > 0);
    // A 24-bit CRC bounds the silent-miscorrection probability ~2^-24 per random
    // codeword. With only <=3 injected bits it must be essentially zero here.
    CHECK(silent <= 2);
}

// --- ADS-L 4 SRD860 issue 2, G.1.7 / G.1.8 / G.1.9 -------------------------
// The three variable-range fields each have an "invalid" code, and encode_unsigned_variable_range
// saturates at exactly that code. The spec is explicit that an out-of-range
// measurement encodes the LIMIT and that "invalid" means unavailable, so
// saturation must stop one short. These pin that apart.

TEST_CASE("adsl: an over-range altitude encodes the limit, not 'unavailable'") {
    AdslPacket packet{};
    packet.reset();
    // G.1.7 worked example: 61104 m or more -> 0x3ffe, invalid -> 0x3fff.
    packet.set_altitude_m(61104);
    CHECK(packet.has_altitude());
    packet.set_altitude_m(100000);  // far past the maximum
    CHECK(packet.has_altitude());
    packet.set_altitude_m(2000000000);
    CHECK(packet.has_altitude());
    // And the spec's other examples still round-trip.
    packet.set_altitude_m(0);
    CHECK(packet.altitude_m() == 0);
    packet.set_altitude_m(1000);
    CHECK(packet.altitude_m() == 1000);
    packet.set_altitude_m(-320);
    CHECK(packet.altitude_m() == -320);
    packet.set_altitude_m(-5000);  // below the minimum clamps to it, not to invalid
    CHECK(packet.altitude_m() == -320);
    CHECK(packet.has_altitude());
}

TEST_CASE("adsl: an over-range ground speed encodes the limit, not 'unavailable'") {
    AdslPacket packet{};
    packet.reset();
    packet.set_speed_q(236 * 4);  // G.1.8 maximum, 236 m/s
    CHECK(packet.has_speed());
    packet.set_speed_q(65535);
    CHECK(packet.has_speed());
}

TEST_CASE("adsl: an over-range sink rate encodes the limit, not 'unavailable'") {
    AdslPacket packet{};
    packet.reset();
    packet.set_climb_e8(952);  // G.1.9 maximum, +119 m/s
    CHECK(packet.has_climb());
    packet.set_climb_e8(-944);  // G.1.9 minimum, -118 m/s
    CHECK(packet.has_climb());
    packet.set_climb_e8(-32768);  // absurd sink: still a rate, not "unknown"
    CHECK(packet.has_climb());
    packet.set_climb_e8(32767);
    CHECK(packet.has_climb());
}

TEST_CASE("adsl: the invalid codes are reachable on purpose and round-trip") {
    AdslPacket packet{};
    packet.reset();
    packet.set_altitude_m(1000);
    packet.set_speed_q(100);
    packet.set_climb_e8(8);
    REQUIRE(packet.has_altitude());
    REQUIRE(packet.has_speed());
    REQUIRE(packet.has_climb());

    packet.set_altitude_invalid();
    packet.set_speed_invalid();
    packet.set_climb_invalid();
    CHECK_FALSE(packet.has_altitude());
    CHECK_FALSE(packet.has_speed());
    CHECK_FALSE(packet.has_climb());

    // Marking one field invalid must not corrupt its packed neighbours: altitude,
    // climb and track share bytes 7..10.
    packet.set_altitude_m(1000);
    packet.set_climb_invalid();
    packet.set_track_c9(256);
    CHECK(packet.altitude_m() == 1000);
    CHECK_FALSE(packet.has_climb());
    CHECK(packet.track_c9() == 256);
    packet.set_climb_e8(-16);
    CHECK(packet.altitude_m() == 1000);
    CHECK(packet.climb_e8() == -16);
    CHECK(packet.track_c9() == 256);
}

TEST_CASE("adsl: from_own marks what own-ship does not know") {
    skyblip::model::Ownship own{};
    own.latitude_1e7 = 485000000;
    own.longitude_1e7 = 85000000;
    own.altitude_hae_mm = 1500000;  // HAE: what G.1.7 transmits
    own.speed_mm_s = 50000;
    own.climb_mm_s = 2000;

    // No fix yet: altitude and ground speed are unavailable, not zero.
    AdslPacket packet{};
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
        AdslPacket packet{};
        CHECK(from_own(packet, own, 0xDD1234u, table, 4));
        CHECK(packet.address() == 0xDD1234u);
        CHECK(packet.address_mapping_table() == table);
    }
}

// Issue #126: the third of three refusals, so a caller past the other two still sends nothing.
TEST_CASE("adsl: without an address the encoder builds no frame, position or name") {
    skyblip::model::Ownship own{};
    own.fix_valid = true;

    AdslPacket untouched{};
    untouched.reset();
    const AdslPacket before = untouched;
    CHECK_FALSE(from_own(untouched, own, std::nullopt, 58, 4));
    CHECK_FALSE(from_own_callsign(untouched, std::nullopt, 58, "D-KXYZ"));
    CHECK(std::memcmp(&untouched, &before, sizeof(AdslPacket)) == 0);
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

    AdslPacket packet{};
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
    CHECK(int(packet.design_assurance) == AdslPacket::kDesignAssuranceNone);
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

// G.1.14 NACp boundaries, in metres of horizontal figure of merit: 7 is under
// 3 m, 6 under 10 m, 5 under 30 m, 4 under 0.05 NM (92.6 m), and 0 is 0.5 NM or
// worse, which is the same code as "no fix".
TEST_CASE("adsl: horizontal accuracy code sits on the G.1.14 boundaries") {
    CHECK(int(AdslPacket::horizontal_accuracy_code(299)) == 7);
    CHECK(int(AdslPacket::horizontal_accuracy_code(300)) == 6);
    CHECK(int(AdslPacket::horizontal_accuracy_code(999)) == 6);
    CHECK(int(AdslPacket::horizontal_accuracy_code(1000)) == 5);
    CHECK(int(AdslPacket::horizontal_accuracy_code(2999)) == 5);
    CHECK(int(AdslPacket::horizontal_accuracy_code(3000)) == 4);
    CHECK(int(AdslPacket::horizontal_accuracy_code(9259)) == 4);
    CHECK(int(AdslPacket::horizontal_accuracy_code(9260)) == 3);
    CHECK(int(AdslPacket::horizontal_accuracy_code(92600)) == 0);
}

// G.1.15 GVA: 3 under 10 m, 2 under 45 m, 1 under 150 m, 0 beyond.
TEST_CASE("adsl: vertical accuracy code sits on the G.1.15 boundaries") {
    CHECK(int(AdslPacket::vertical_accuracy_code(999)) == 3);
    CHECK(int(AdslPacket::vertical_accuracy_code(1000)) == 2);
    CHECK(int(AdslPacket::vertical_accuracy_code(4499)) == 2);
    CHECK(int(AdslPacket::vertical_accuracy_code(4500)) == 1);
    CHECK(int(AdslPacket::vertical_accuracy_code(14999)) == 1);
    CHECK(int(AdslPacket::vertical_accuracy_code(15000)) == 0);
}

// A degrading fix walks the codes down together, and a hopeless one claims
// nothing rather than claiming a number nobody should act on.
TEST_CASE("adsl: a degrading HDOP walks the accuracy claim down") {
    skyblip::model::Ownship own{};
    own.fix_valid = true;
    AdslPacket packet{};

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
