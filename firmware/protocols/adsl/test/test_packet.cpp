// The ADS-L position packet end to end, a radio protocol checked with no RF
// hardware in the room: the channel is an injectable bit error rate, and the
// packet has to survive it or say that it did not. What these separate is a
// *detected failure*, a frame dropped and counted, from a *silent
// miscorrection*, a frame that decodes to a plausible aircraft somewhere it is
// not. Only the second one collides with anybody.
#include <cstdlib>  // std::abs - libc++ pulls it in transitively, libstdc++ does not
#include <cstring>
#include <initializer_list>  // same: a braced list in a range-for needs it named
#include <utility>

#include "doctest/doctest.h"
#include "protocols/adsl/packet.h"
#include "protocols/common/test/channel.h"

using namespace skyblip;

namespace {
adsl::Packet make_reference() {
    adsl::Packet packet;
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
    adsl::Packet packet = make_reference();
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
    adsl::Packet packet = make_reference();
    adsl::Packet golden = packet;
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
    adsl::Packet packet = make_reference();
    packet.scramble();
    packet.set_crc();
    uint8_t error[adsl::Packet::kDataBytes] = {0};
    // flip a data bit (in the CRC-covered region)
    packet.data[7] ^= 0x08;
    CHECK(packet.check_crc() != 0);
    int corrected = packet.correct(error, 6);
    CHECK(corrected == 1);
    CHECK(packet.check_crc() == 0);
}

TEST_CASE("adsl: multi-bit errors within flagged (weak) bits are corrected") {
    adsl::Packet packet = make_reference();
    packet.scramble();
    packet.set_crc();
    uint8_t* data = packet.data;
    uint8_t error[adsl::Packet::kDataBytes] = {0};
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
    adsl::Packet packet = make_reference();
    packet.scramble();
    packet.set_crc();
    const adsl::Packet sent = packet;
    uint8_t error[adsl::Packet::kDataBytes] = {0};
    for (int byte = 2; byte < 12; byte++) error[byte] = 0x10;
    for (int byte : {9, 10, 11}) packet.data[byte] ^= 0x10;
    REQUIRE(packet.check_crc() != 0);

    CHECK(packet.correct(error, 16) == 3);
    CHECK(std::memcmp(packet.data, sent.data, adsl::Packet::kDataBytes) == 0);
}

TEST_CASE("adsl: one unmarked error is repaired even when more bits are marked than the cap") {
    adsl::Packet packet = make_reference();
    packet.scramble();
    packet.set_crc();
    const adsl::Packet sent = packet;
    uint8_t error[adsl::Packet::kDataBytes] = {0};
    error[0] = 0xFE;
    packet.data[6] ^= 0x20;
    REQUIRE(packet.check_crc() != 0);

    adsl::Packet no_cap = packet;
    CHECK(packet.correct(error, 6) == 1);
    CHECK(std::memcmp(packet.data, sent.data, adsl::Packet::kDataBytes) == 0);
    error[1] = 0xFF;
    error[2] = 0xFF;
    CHECK(no_cap.correct(error, -1) == 1);
    CHECK(std::memcmp(no_cap.data, sent.data, adsl::Packet::kDataBytes) == 0);
}

TEST_CASE("adsl: a frame no repair fits is left as it arrived") {
    adsl::Packet packet = make_reference();
    packet.scramble();
    packet.set_crc();
    uint8_t error[adsl::Packet::kDataBytes] = {0};
    error[0] = 0x80;
    for (int byte : {4, 9, 14}) packet.data[byte] ^= 0x03;
    const adsl::Packet arrived = packet;

    CHECK(packet.correct(error, 6) == -1);
    CHECK(std::memcmp(packet.data, arrived.data, adsl::Packet::kDataBytes) == 0);
}

// G.1.5 steps: 93206 to a degree of latitude, 46603 of longitude. A position takes the nearest.
TEST_CASE("adsl: a position goes out as the nearest whole G.1.5 step") {
    adsl::Packet packet{};
    packet.reset();
    packet.set_latitude_1e7(-847540127);
    packet.set_longitude_1e7(-847540127);
    CHECK(packet.latitude_steps() == -7899583);
    CHECK(packet.longitude_steps() == -3949791);
    packet.set_longitude_1e7(1800000000);
    CHECK(packet.longitude_steps() == 8388540);
}

TEST_CASE("adsl: Monte-Carlo BER, detected vs silent miscorrection accounting") {
    // Push random-ish bit errors, some flagged (weak) and some not, and count:
    //   good      = decoded to the exact original codeword
    //   detected  = check_crc != 0 after correct() (safe: we know it failed)
    //   silent    = check_crc == 0 but payload != original (the DANGER mode)
    // We assert silent miscorrection stays rare and is *counted*, never hidden.
    wire::Random random(0xC0FFEE);
    const int kTrials = 20000;
    int good = 0, detected = 0, silent = 0;

    for (int trial = 0; trial < kTrials; trial++) {
        adsl::Packet tx = make_reference();
        tx.set_address(0x100000 + (random.next() & 0xFFFFF));
        tx.scramble();
        tx.set_crc();

        adsl::Packet rx = tx;
        uint8_t* data = rx.data;
        uint8_t error[adsl::Packet::kDataBytes] = {0};

        // Inject up to 3 bit errors, and flag ~70% of them as weak.
        int errors = (random.next() % 4);  // 0..3
        for (int injected = 0; injected < errors; injected++) {
            int bit = random.next() % (adsl::Packet::kDataBytes * 8);
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
// The three exponential fields each have an "invalid" code, and Exponential::encode
// saturates at exactly that code. The spec is explicit that an out-of-range
// measurement encodes the LIMIT and that "invalid" means unavailable, so
// saturation must stop one short. These pin that apart.

TEST_CASE("adsl: an over-range altitude encodes the limit, not 'unavailable'") {
    adsl::Packet packet{};
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
    adsl::Packet packet{};
    packet.reset();
    packet.set_speed_q(236 * 4);  // G.1.8 maximum, 236 m/s
    CHECK(packet.has_speed());
    packet.set_speed_q(65535);
    CHECK(packet.has_speed());
}

TEST_CASE("adsl: an over-range sink rate encodes the limit, not 'unavailable'") {
    adsl::Packet packet{};
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
    adsl::Packet packet{};
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

// G.1.14 NACp boundaries, in metres of horizontal figure of merit: 7 is under
// 3 m, 6 under 10 m, 5 under 30 m, 4 under 0.05 NM (92.6 m), and 0 is 0.5 NM or
// worse, which is the same code as "no fix".
TEST_CASE("adsl: horizontal accuracy code sits on the G.1.14 boundaries") {
    CHECK(int(adsl::Packet::horizontal_accuracy_code(299)) == 7);
    CHECK(int(adsl::Packet::horizontal_accuracy_code(300)) == 6);
    CHECK(int(adsl::Packet::horizontal_accuracy_code(999)) == 6);
    CHECK(int(adsl::Packet::horizontal_accuracy_code(1000)) == 5);
    CHECK(int(adsl::Packet::horizontal_accuracy_code(2999)) == 5);
    CHECK(int(adsl::Packet::horizontal_accuracy_code(3000)) == 4);
    CHECK(int(adsl::Packet::horizontal_accuracy_code(9259)) == 4);
    CHECK(int(adsl::Packet::horizontal_accuracy_code(9260)) == 3);
    CHECK(int(adsl::Packet::horizontal_accuracy_code(92600)) == 0);
}

// G.1.15 GVA: 3 under 10 m, 2 under 45 m, 1 under 150 m, 0 beyond.
TEST_CASE("adsl: vertical accuracy code sits on the G.1.15 boundaries") {
    CHECK(int(adsl::Packet::vertical_accuracy_code(999)) == 3);
    CHECK(int(adsl::Packet::vertical_accuracy_code(1000)) == 2);
    CHECK(int(adsl::Packet::vertical_accuracy_code(4499)) == 2);
    CHECK(int(adsl::Packet::vertical_accuracy_code(4500)) == 1);
    CHECK(int(adsl::Packet::vertical_accuracy_code(14999)) == 1);
    CHECK(int(adsl::Packet::vertical_accuracy_code(15000)) == 0);
}

TEST_CASE("adsl: a field reads back the middle of the step its code names") {
    adsl::Packet packet{};
    packet.reset();
    for (auto pair : {std::pair<int, int>{0, 0}, {63, 63}, {64, 65}, {200, 202}, {5000, 948}}) {
        packet.set_speed_q(static_cast<uint16_t>(pair.first));
        CHECK(int(packet.speed_q()) == pair.second);
    }
    for (auto pair :
         {std::pair<int, int>{-500, -320}, {0, 0}, {4000, 4001}, {20000, 20002}, {70000, 61108}}) {
        packet.set_altitude_m(pair.first);
        CHECK(packet.altitude_m() == pair.second);
    }
    for (auto pair : {std::pair<int, int>{-32768, -948},
                      {-2000, -948},
                      {-100, -101},
                      {-1, -1},
                      {0, 0},
                      {80, 81},
                      {2000, 956}}) {
        packet.set_climb_e8(static_cast<int16_t>(pair.first));
        CHECK(int(packet.climb_e8()) == pair.second);
    }
}

// The frame a fix becomes in skyBlip, written here from the fields it decodes to.
TEST_CASE("adsl: a Traffic payload is byte for byte the frame other receivers already read") {
    adsl::Traffic traffic;
    traffic.address = 0x2ABCDE;
    traffic.address_mapping_table = 58;
    traffic.timestamp = 48;
    traffic.aircraft_category = 1;
    traffic.emergency = 1;
    traffic.position_valid = true;
    traffic.latitude_1e7 = -337654336;
    traffic.longitude_1e7 = 1512345772;
    traffic.altitude_valid = true;
    traffic.altitude_m = 2346;
    traffic.speed_valid = true;
    traffic.speed_q = 246;
    traffic.climb_valid = true;
    traffic.climb_e8 = -26;
    traffic.track_c9 = 389;
    traffic.integrity.horizontal_accuracy = 5;
    traffic.integrity.vertical_accuracy = 2;
    traffic.integrity.velocity_accuracy = 1;
    adsl::Packet packet{};
    adsl::encode_traffic(packet, traffic);
    const uint8_t clear[] = {0x00, 0x02, 0xBA, 0x37, 0xAF, 0x0A, 0x30, 0x21,
                             0x7B, 0xFA, 0xCF, 0x31, 0x8B, 0x6B, 0x8D, 0x6A,
                             0x8A, 0xC6, 0xC2, 0x00, 0x35, 0x00, 0x00, 0x00};
    CHECK(std::memcmp(packet.data, clear, sizeof(clear)) == 0);

    packet.scramble();
    packet.set_crc();
    const uint8_t air[] = {0x00, 0x5E, 0x6F, 0xC1, 0xDB, 0x84, 0x85, 0x87, 0xBC, 0xBF, 0x23, 0x61,
                           0x44, 0x47, 0x51, 0xC5, 0xA3, 0x07, 0x3D, 0xE0, 0x15, 0x22, 0x93, 0x11};
    CHECK(std::memcmp(packet.data, air, sizeof(air)) == 0);

    adsl::Traffic heard;
    packet.descramble();
    REQUIRE(adsl::decode_traffic(packet, heard));
    CHECK(heard.address == 0x2ABCDE);
    CHECK(heard.latitude_1e7 == traffic.latitude_1e7);
    CHECK(heard.altitude_m == traffic.altitude_m);
}
