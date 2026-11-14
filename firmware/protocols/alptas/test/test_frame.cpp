// ALP-TAS (FLARM-wire, 2024 protocol) air-frame codec. Two things are worth
// testing here and both are safety-critical: that a frame we transmit is the
// frame the protocol says (encode -> decode round trip, at several latitudes and
// across the 20-bit position wrap), and that a frame that is NOT ours is thrown
// away (frame CRC, plus the decrypt-plausibility gate that is the only thing
// standing between a mis-framed foreign packet and the traffic table).
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#include "doctest/doctest.h"
#include "protocols/alptas/frame.h"
#include "protocols/common/test/channel.h"

using namespace skyblip;
using namespace skyblip::alptas;
using wire::Status;

namespace {
constexpr uint32_t kUtcS = 1700000000;  // 1700000000 % 16 == 0, so timebits == 0

Position make_position(int32_t latitude_1e7, int32_t longitude_1e7) {
    Position position;
    position.address = 0x123456;
    position.address_type = kAddressTypeFlarm;
    position.aircraft_type = 0x1;  // glider
    position.airborne = kAirborneFlying;
    position.latitude_1e7 = latitude_1e7;
    position.longitude_1e7 = longitude_1e7;
    position.altitude_m = 1234;
    position.speed_e1 = 450;  // 45 m/s
    position.climb_e1 = -55;  // -5.5 m/s
    position.course_deg = 180;
    return position;
}

// Longitude quantum in 1e-7 deg at this latitude, from the protocol's own table:
// this is the tolerance a longitude round trip is entitled to.
int32_t longitude_quantum(int32_t latitude_1e7) {
    int32_t degrees = std::abs(latitude_1e7) / 10000000;
    static const int32_t kStep[] = {52, 65, 80, 101, 129, 174, 252, 362, 552, 806};
    if (degrees >= 89) return kStep[9];
    if (degrees >= 84) return kStep[8];
    if (degrees >= 79) return kStep[7];
    if (degrees >= 74) return kStep[6];
    if (degrees >= 69) return kStep[5];
    if (degrees >= 64) return kStep[4];
    if (degrees >= 54) return kStep[3];
    if (degrees >= 39) return kStep[2];
    if (degrees >= 26) return kStep[1];
    return 806;  // never tighter than the coarsest step; latitudes above are exact
}

void check_position_round_trip(int32_t latitude_1e7, int32_t longitude_1e7,
                               int32_t reference_latitude_1e7, int32_t reference_longitude_1e7) {
    CAPTURE(latitude_1e7);
    CAPTURE(longitude_1e7);
    uint8_t frame[kFrameBytes];
    encode(frame, make_position(latitude_1e7, longitude_1e7), kUtcS);
    Position got;
    REQUIRE(decode(frame, kUtcS, reference_latitude_1e7, reference_longitude_1e7, got) ==
            Status::Ok);
    CHECK(std::abs(got.latitude_1e7 - latitude_1e7) <= 26);
    CHECK(std::abs(got.longitude_1e7 - longitude_1e7) <= longitude_quantum(latitude_1e7));
}

void encoded(uint8_t* frame) { encode(frame, make_position(481234567, 87654321), kUtcS); }
}  // namespace

TEST_CASE("alptas: encode then decode preserves the identity and motion fields") {
    uint8_t frame[kFrameBytes];
    encoded(frame);

    Position got;
    REQUIRE(decode(frame, kUtcS, 480000000, 87000000, got) == Status::Ok);

    CHECK(got.address == 0x123456u);
    CHECK(int(got.address_type) == kAddressTypeFlarm);
    CHECK(int(got.aircraft_type) == 0x1);
    CHECK(int(got.airborne) == kAirborneFlying);
    CHECK(got.altitude_m == 1234);
    CHECK(got.speed_e1 == 450);
    CHECK(got.climb_e1 == -55);
    CHECK(got.course_deg == 180);
}

TEST_CASE("alptas: position round trips north, south, high latitude and near zero") {
    check_position_round_trip(481234567, 87654321, 480000000, 87000000);      // Black Forest
    check_position_round_trip(-339876543, 184321098, -340000000, 184000000);  // Cape Town
    check_position_round_trip(695000000, -202500000, 695500000, -202000000);  // Arctic, west
    check_position_round_trip(-15000, -25000, 10000, 10000);                  // across 0/0
    check_position_round_trip(895000000, 1795000000, 894000000, 1794000000);  // near the pole
}

TEST_CASE("alptas: position round trips across the 20-bit coding wrap") {
    // At |lat| < 14 deg the longitude quantum is 52e-7 deg, so the 20-bit field
    // wraps every 52 * 2^20 = 5.4525952 deg of longitude. A receiver one side of
    // that boundary must still place a target on the other side.
    constexpr int32_t kWrap = 52 * 1048576;  // 54525952
    check_position_round_trip(50000000, kWrap + 1900, kWrap - 9000, 50000000);
    check_position_round_trip(50000000, kWrap - 1900, kWrap + 9000, 50000000);
    // Latitude wraps every 52 * 2^20 too, at 5.4525952 deg.
    check_position_round_trip(kWrap + 1300, 50000000, kWrap - 7000, 50000000);
    check_position_round_trip(-(kWrap + 1300), 50000000, -(kWrap - 7000), 50000000);
}

// The speed field counts decimetres a second in 8 bits behind two exponent bits, the climb in 6.
TEST_CASE("alptas: speed, climb and course come back within the step their codes name") {
    for (int32_t speed : {0, 10, 100, 255, 450, 1000, 2000}) {
        for (int32_t climb : {0, 10, -10, 55, -55, 100, -100, 250}) {
            for (uint16_t course : {uint16_t(0), uint16_t(1), uint16_t(90), uint16_t(180),
                                    uint16_t(269), uint16_t(359)}) {
                Position position = make_position(481234567, 87654321);
                position.speed_e1 = speed;
                position.climb_e1 = climb;
                position.course_deg = course;
                uint8_t frame[kFrameBytes];
                encode(frame, position, kUtcS);
                Position got;
                REQUIRE(decode(frame, kUtcS, 481000000, 87000000, got) == Status::Ok);
                CAPTURE(speed);
                CAPTURE(climb);
                CAPTURE(course);
                CHECK(std::abs(got.speed_e1 - speed) <= 3);
                CHECK(std::abs(got.climb_e1 - climb) <= 3);
                CHECK(got.course_deg == course);
            }
        }
    }
}

TEST_CASE("alptas: altitude round trips including below sea level and high up") {
    static const int32_t kAltitudes[] = {-400, -50, 0, 300, 1234, 3096, 5000, 11000};
    for (int32_t altitude : kAltitudes) {
        Position position = make_position(481234567, 87654321);
        position.altitude_m = altitude;
        uint8_t frame[kFrameBytes];
        encode(frame, position, kUtcS);
        Position got;
        REQUIRE(decode(frame, kUtcS, 481000000, 87000000, got) == Status::Ok);
        CAPTURE(altitude);
        CHECK(std::abs(got.altitude_m - altitude) <= 2);
    }
}

TEST_CASE("alptas: the frame CRC is set, checked, and a flipped bit fails it") {
    uint8_t frame[kFrameBytes];
    encoded(frame);
    CHECK(crc_ok(frame));

    uint16_t check = crc(frame);
    CHECK(int(frame[kDataBytes]) == int(check >> 8));  // high byte first on the wire
    CHECK(int(frame[kDataBytes + 1]) == int(check & 0xFF));

    for (int bit = 0; bit < kFrameBytes * 8; bit++) {
        uint8_t bad[kFrameBytes];
        std::memcpy(bad, frame, sizeof(bad));
        bad[bit >> 3] ^= static_cast<uint8_t>(1u << (bit & 7));
        CAPTURE(bit);
        CHECK_FALSE(crc_ok(bad));
        Position got;
        CHECK(decode(bad, kUtcS, 480000000, 87000000, got) == Status::Crc);
    }

    // set_crc is idempotent and repairs a clobbered checksum.
    frame[kDataBytes] ^= 0xFF;
    CHECK_FALSE(crc_ok(frame));
    set_crc(frame);
    CHECK(crc_ok(frame));
}

TEST_CASE("alptas: the decrypt gate accepts the second it was sent in, +/-1") {
    uint8_t frame[kFrameBytes];
    encoded(frame);

    Position got;
    CHECK(decode(frame, kUtcS, 480000000, 87000000, got) == Status::Ok);
    CHECK(decode(frame, kUtcS + 1, 480000000, 87000000, got) == Status::Ok);

    // Two seconds off is a different aircraft's frame, or ours mis-framed.
    CHECK(decode(frame, kUtcS + 2, 480000000, 87000000, got) != Status::Ok);
    // No wrap fudge: 15 seconds later the low nibble is 15 against a sent 0, and
    // that is a rejection, not an off-by-one.
    CHECK(decode(frame, kUtcS + 15, 480000000, 87000000, got) != Status::Ok);
    // A different 16-second block changes the key stage as well.
    CHECK(decode(frame, kUtcS + 16, 480000000, 87000000, got) != Status::Ok);
    CHECK(decode(frame, kUtcS - 100, 480000000, 87000000, got) != Status::Ok);
}

TEST_CASE("alptas: the decrypt gate rejects a frame with one byte changed") {
    uint8_t frame[kFrameBytes];
    encoded(frame);

    // Change one byte and repair the CRC, so the only line of defence left is the
    // plausibility of the decrypt itself.
    for (int i = 0; i < kDataBytes; i++) {
        uint8_t bad[kFrameBytes];
        std::memcpy(bad, frame, sizeof(bad));
        bad[i] = static_cast<uint8_t>(bad[i] ^ 0x5A);
        set_crc(bad);
        REQUIRE(crc_ok(bad));
        Position got;
        CAPTURE(i);
        CHECK(decode(bad, kUtcS, 480000000, 87000000, got) != Status::Ok);
    }
}

TEST_CASE("alptas: the decrypt gate rejects random frames") {
    wire::Random random(0xA1B2C3D4);
    int accepted = 0;
    for (int trial = 0; trial < 500; trial++) {
        uint8_t frame[kFrameBytes];
        for (int i = 0; i < kDataBytes; i++) frame[i] = static_cast<uint8_t>(random.next() >> 9);
        set_crc(frame);
        Position got;
        if (decode(frame, kUtcS, 480000000, 87000000, got) == Status::Ok) accepted++;
    }
    CHECK(accepted == 0);
}

TEST_CASE("alptas: a non-position message type is not decoded as traffic") {
    uint8_t frame[kFrameBytes];
    encoded(frame);
    // Message type lives in the plaintext first word: 3 is a text message.
    frame[3] = static_cast<uint8_t>((frame[3] & 0xF0) | 3);
    set_crc(frame);
    Position got;
    CHECK(decode(frame, kUtcS, 480000000, 87000000, got) == Status::Unsupported);
}

TEST_CASE("alptas: a position that unwraps past the pole or the antimeridian is not a target") {
    constexpr int32_t kReferenceLatitude1e7 = 895000000;
    constexpr int32_t kReferenceLongitude1e7 = 1799900000;
    check_position_round_trip(895000000, 1799000000, kReferenceLatitude1e7, kReferenceLongitude1e7);

    uint8_t frame[kFrameBytes];
    Position got;
    // 635974 quanta of 806e-7 deg, unwrapped against 179.99E, is 2 * 2^20 further east: 220 deg
    encode(frame, make_position(kReferenceLatitude1e7, 512595044), kUtcS);
    CHECK(decode(frame, kUtcS, kReferenceLatitude1e7, kReferenceLongitude1e7, got) ==
          Status::Invalid);

    // 722784 quanta of 52e-7 deg, unwrapped against 89.99N, is 16 * 2^20 further north: 91 deg
    encode(frame, make_position(37584768, 0), kUtcS);
    CHECK(decode(frame, kUtcS, 899900000, 0, got) == Status::Invalid);
}

TEST_CASE("alptas: the chip pairs the air destroyed are flipped back to the frame that was sent") {
    uint8_t sent[kFrameBytes];
    encoded(sent);

    for (int seed = 1; seed <= 20; seed++) {
        uint8_t frame[kFrameBytes];
        std::memcpy(frame, sent, sizeof(frame));
        uint8_t erasures[kFrameBytes] = {0};
        wire::RfChannel channel(static_cast<uint32_t>(seed));
        const int flipped = channel.apply_bit_error_rate(frame, sizeof(frame), 0.012, erasures);
        CAPTURE(seed);
        CAPTURE(flipped);
        if (flipped == 0 || flipped > 6) continue;
        CHECK(correct(frame, erasures) == flipped);
        CHECK(std::memcmp(frame, sent, sizeof(frame)) == 0);
        Position got;
        CHECK(decode(frame, kUtcS, 480000000, 87000000, got) == Status::Ok);
    }
}

// A correction that half-applied would hand the decrypt a frame no transmitter sent.
TEST_CASE("alptas: damage nothing flagged, or too much of it, leaves the frame as it arrived") {
    uint8_t sent[kFrameBytes];
    encoded(sent);

    uint8_t unflagged[kFrameBytes];
    std::memcpy(unflagged, sent, sizeof(unflagged));
    uint8_t none[kFrameBytes] = {0};
    unflagged[7] ^= 0x10;
    uint8_t as_arrived[kFrameBytes];
    std::memcpy(as_arrived, unflagged, sizeof(as_arrived));
    CHECK(correct(unflagged, none) == -1);
    CHECK(std::memcmp(unflagged, as_arrived, sizeof(as_arrived)) == 0);

    uint8_t beyond[kFrameBytes];
    std::memcpy(beyond, sent, sizeof(beyond));
    uint8_t erasures[kFrameBytes] = {0};
    wire::RfChannel channel(7);
    channel.apply_bit_error_rate(beyond, sizeof(beyond), 0.25, erasures);
    std::memcpy(as_arrived, beyond, sizeof(as_arrived));
    CHECK(correct(beyond, erasures) == -1);
    CHECK(std::memcmp(beyond, as_arrived, sizeof(as_arrived)) == 0);

    CHECK(correct(sent, none) == 0);  // a frame that arrived whole is not touched
}

// The whole of skyblip#2026-09-19: an hour of DEC that was two clocks disagreeing.
TEST_CASE("alptas: a frame our second refuses names the second its sender keyed") {
    for (int32_t offset : {-18, -2, -1, 0, 1, 5, 16, 18}) {
        uint8_t frame[kFrameBytes];
        encode(frame, make_position(481234567, 87654321),
               static_cast<uint32_t>(static_cast<int32_t>(kUtcS) + offset));
        uint32_t keyed = 0;
        CAPTURE(offset);
        REQUIRE(keyed_second(frame, kUtcS, keyed) == Status::Ok);
        CHECK(static_cast<int32_t>(keyed - kUtcS) == offset);
    }
}

TEST_CASE("alptas: a second further out than a clock can plausibly be is not searched for") {
    uint8_t frame[kFrameBytes];
    encode(frame, make_position(481234567, 87654321), kUtcS + 40);
    uint32_t keyed = 0;
    CHECK(keyed_second(frame, kUtcS, keyed) == Status::Invalid);

    // A frame nothing keyed: noise that framed and held its CRC by accident is not a clock fault.
    uint8_t noise[kFrameBytes];
    for (int i = 0; i < kDataBytes; i++) noise[i] = static_cast<uint8_t>(i * 37 + 11);
    noise[3] = static_cast<uint8_t>((noise[3] & 0xF0) | kMessageTypePosition);
    set_crc(noise);
    CHECK(keyed_second(noise, kUtcS, keyed) == Status::Invalid);

    uint8_t broken[kFrameBytes];
    std::memcpy(broken, frame, sizeof(broken));
    broken[5] ^= 0x01;
    CHECK(keyed_second(broken, kUtcS, keyed) == Status::Crc);

    uint8_t other[kFrameBytes];
    encoded(other);
    other[3] = static_cast<uint8_t>(other[3] & 0xF0);
    set_crc(other);
    CHECK(keyed_second(other, kUtcS, keyed) == Status::Unsupported);
}

// A codec that agrees with itself can still put a frame on air no other receiver reads.
TEST_CASE("alptas: a frame is byte for byte the frame other receivers already read") {
    Position position;
    position.address = 0xDD1234;
    position.address_type = kAddressTypeFlarm;
    position.aircraft_type = 0x3;
    position.airborne = kAirborneFlying;
    position.latitude_1e7 = 465432100;
    position.longitude_1e7 = 72345600;
    position.altitude_m = 3456;
    position.speed_e1 = 555;
    position.climb_e1 = 46;
    position.course_deg = 54;
    uint8_t frame[kFrameBytes];
    encode(frame, position, 1700000005);
    const uint8_t north[] = {0x34, 0x12, 0xDD, 0x22, 0x00, 0x00, 0x00, 0x33, 0xD2,
                             0x72, 0x5F, 0x76, 0x76, 0xB5, 0xFE, 0xAE, 0x91, 0x3E,
                             0xAA, 0x14, 0x28, 0x24, 0xA2, 0x6B, 0x12, 0xCC};
    CHECK(std::memcmp(frame, north, sizeof(north)) == 0);

    position.latitude_1e7 = -823456700;
    position.altitude_m = -50;
    position.climb_e1 = -500;
    position.speed_e1 = 2250;
    encode(frame, position, 1700000031);
    const uint8_t south[] = {0x34, 0x12, 0xDD, 0x22, 0x00, 0x00, 0x00, 0x33, 0x95,
                             0x9B, 0xFF, 0x4F, 0x6F, 0x10, 0x70, 0x9D, 0xC7, 0x22,
                             0xCC, 0x2B, 0xBF, 0xE9, 0xB5, 0xAE, 0xD7, 0x04};
    CHECK(std::memcmp(frame, south, sizeof(south)) == 0);

    Position got;
    REQUIRE(decode(frame, 1700000031, -823000000, 72000000, got) == Status::Ok);
    CHECK(got.latitude_1e7 == -823456712);
    CHECK(got.longitude_1e7 == 72345700);
    CHECK(got.altitude_m == -50);
    CHECK(got.speed_e1 == 2248);
    CHECK(got.climb_e1 == -496);
    CHECK(got.course_deg == 54);
}
