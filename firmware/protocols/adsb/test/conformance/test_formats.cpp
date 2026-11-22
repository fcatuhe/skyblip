// DO-260B 2.2.3.2 to 2.2.3.2.5: the DF17 frame and its position and identification messages.
#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "doctest/doctest.h"
#include "protocols/adsb/message.h"
#include "protocols/adsb/squitter.h"
#include "protocols/adsb/test/conformance/reference.h"

using namespace skyblip;
using namespace skyblip::adsb;
using namespace skyblip::adsb::conformance;
using skyblip::wire::Status;

namespace {

adsb::Squitter airborne(uint8_t type_code, uint32_t altitude_field) {
    adsb::Squitter frame = squitter(type_code);
    put(frame, 9, 20, altitude_field);
    put(frame, 21, 21, 1);
    put(frame, 22, 22, 1);
    put(frame, 23, 39, 0x15555);
    put(frame, 40, 56, 0x0AAAA);
    return frame;
}

adsb::Squitter surface(uint8_t type_code, uint8_t movement) {
    adsb::Squitter frame = squitter(type_code);
    put(frame, 6, 12, movement);
    put(frame, 13, 13, 1);
    put(frame, 14, 20, 64);
    put(frame, 21, 21, 1);
    put(frame, 22, 22, 0);
    put(frame, 23, 39, 0x12345);
    put(frame, 40, 56, 0x0F0F0);
    return frame;
}

adsb::Squitter identification(uint8_t type_code, uint8_t category, const char* callsign) {
    return adsb::identification(kAddress, kAirborneCapability, type_code, category, callsign);
}

// INFO: fc 17nov26 Annex 10's Gray steps written apart from the decoder, 100 ft counts reflected
uint32_t gillham_field(int32_t feet) {
    const uint32_t above = static_cast<uint32_t>(feet + 1300);
    uint32_t five_hundreds = above / 500;
    uint32_t hundreds = (above % 500) / 100;
    if (hundreds == 0) {
        five_hundreds--;
        hundreds = 5;
    }
    if (five_hundreds & 1u) hundreds = 6 - hundreds;
    const uint32_t c_count = hundreds == 5 ? 7 : hundreds;
    const uint32_t d_to_b = five_hundreds ^ (five_hundreds >> 1);
    const uint32_t c = c_count ^ (c_count >> 1);
    const auto bit = [](uint32_t value, int index) { return (value >> index) & 1u; };
    const uint32_t c1 = bit(c, 2), c2 = bit(c, 1), c4 = bit(c, 0);
    const uint32_t d2 = bit(d_to_b, 7), d4 = bit(d_to_b, 6), a1 = bit(d_to_b, 5),
                   a2 = bit(d_to_b, 4), a4 = bit(d_to_b, 3), b1 = bit(d_to_b, 2),
                   b2 = bit(d_to_b, 1), b4 = bit(d_to_b, 0);
    const uint32_t order[] = {c1, a1, c2, a2, c4, a4, b1, 0, b2, d2, b4, d4};
    uint32_t field = 0;
    for (uint32_t pulse : order) field = (field << 1) | pulse;
    return field;
}

uint32_t q_field(uint32_t n) { return ((n >> 4) << 5) | 0x10u | (n & 0xFu); }

}

TEST_CASE("DO-260B 2.2.3.2: a DF17 frame is DF, CA, AA, a 56-bit ME and the PI parity") {
    adsb::Squitter frame = airborne(11, q_field(1000));
    frame.bytes[0] = static_cast<uint8_t>((17 << 3) | 6);
    const Message message = heard_ok(frame);
    CHECK(message.capability == 6);
    CHECK(message.address == kAddress);
    CHECK(message.type_code == 11);
    CHECK(message.kind == Kind::AirbornePosition);
}

TEST_CASE("DO-260B 2.2.3.2.1: the baseline fields sit at message bits 1, 6, 9, 33 and 89") {
    const adsb::Squitter frame = airborne(11, q_field(1000));
    CHECK((frame.bytes[0] >> 3) == 17);
    CHECK((frame.bytes[0] & 7) == kAirborneCapability);
    const uint32_t address =
        uint32_t{frame.bytes[1]} << 16 | uint32_t{frame.bytes[2]} << 8 | frame.bytes[3];
    CHECK(address == kAddress);
    CHECK((frame.bytes[4] >> 3) == 11);
    CHECK(heard_ok(frame).address == kAddress);
}

// Each DF is read by its own rule, so no other format becomes a DF17 message.
TEST_CASE(
    "DO-260B 2.2.3.2.1.1: DF17 is read and DF11, DF16, DF18 and DF19 are never read as DF17") {
    CHECK(hear(airborne(11, q_field(1000))).status == Status::Ok);
    for (uint8_t df : {11, 16, 18, 19}) {
        adsb::Squitter frame = airborne(11, q_field(1000));
        frame.bytes[0] = static_cast<uint8_t>((df << 3) | 5);
        const Heard heard = hear(frame);
        CHECK(heard.message.downlink_format == df);
        CHECK((heard.status != Status::Ok || heard.message.kind == Kind::AllCall ||
               heard.message.kind == Kind::Reply));
    }
    for (uint8_t df : {18, 19}) {
        adsb::Squitter frame = airborne(11, q_field(1000));
        frame.bytes[0] = static_cast<uint8_t>(df << 3);
        CHECK(hear(frame).status == Status::Unsupported);
    }
}

TEST_CASE("DO-260B 2.2.3.2.1.2: every CA code of Table 2-8 is carried as sent") {
    for (uint8_t capability = 0; capability < 8; capability++) {
        adsb::Squitter frame = airborne(11, q_field(1000));
        frame.bytes[0] = static_cast<uint8_t>((17 << 3) | capability);
        CHECK(heard_ok(frame).capability == capability);
    }
}

TEST_CASE("DO-260B 2.2.3.2.1.5: the AA field is the sender's 24-bit ICAO address") {
    for (uint32_t address : {0x000001u, 0x3C4D5Eu, 0xFFFFFEu}) {
        adsb::Squitter frame = adsb::airborne_position(address, 5, 48.0, 8.0, false, 3000);
        CHECK(heard_ok(frame).address == address);
    }
}

TEST_CASE("DO-260B 2.2.3.2.1.6: the ME field's TYPE code picks the message its bits are read as") {
    CHECK(heard_ok(identification(4, 0, "KLM1023")).kind == Kind::Identification);
    CHECK(heard_ok(surface(7, 10)).kind == Kind::SurfacePosition);
    CHECK(heard_ok(airborne(11, q_field(1000))).kind == Kind::AirbornePosition);
    CHECK(heard_ok(velocity(1)).kind == Kind::Velocity);
    CHECK(heard_ok(adsb::operational_status(kAddress, 5, 2, 2)).kind == Kind::OperationalStatus);
}

TEST_CASE("DO-260B 2.2.3.2.1.7: the PI field is the parity over an all-zero overlay") {
    adsb::Squitter frame = airborne(11, q_field(1000));
    adsb::seal(frame.bytes);
    CHECK(wire::crc24_check(frame.bytes, kFrameBytes) == 0);
    CHECK(hear_as_sent(frame).status == Status::Ok);
    frame.bytes[13] ^= 0x01;
    frame.bytes[12] ^= 0x01;
    CHECK(hear_as_sent(frame).status == Status::Crc);
}

TEST_CASE(
    "DO-260B 2.2.3.2.2: Table 2-14's TYPE and subtype codes name each message or reserve it") {
    for (uint8_t type_code = 1; type_code <= 4; type_code++)
        CHECK(heard_ok(identification(type_code, 0, "A")).kind == Kind::Identification);
    for (uint8_t type_code = 5; type_code <= 8; type_code++)
        CHECK(heard_ok(surface(type_code, 10)).kind == Kind::SurfacePosition);
    for (uint8_t type_code : {9, 18, 20, 22})
        CHECK(heard_ok(airborne(type_code, q_field(1000))).kind == Kind::AirbornePosition);
    for (uint8_t subtype = 1; subtype <= 4; subtype++)
        CHECK(heard_ok(velocity(subtype)).kind == Kind::Velocity);
    for (uint8_t subtype : {0, 5, 6, 7})
        CHECK(hear(squitter(19, subtype)).status == Status::Unsupported);
    for (uint8_t type_code : {23, 24, 25, 26, 27, 30})
        CHECK(hear(squitter(type_code)).status == Status::Unsupported);
}

TEST_CASE(
    "DO-260B 2.2.3.2.3: an airborne position is TYPE, status, NIC-B, altitude, T, F and CPR") {
    const Message message = heard_ok(airborne(11, q_field(1440)));
    CHECK(message.airborne_position.pressure_altitude_valid);
    CHECK(message.airborne_position.pressure_altitude_ft == 35000);
    CHECK(message.airborne_position.utc_synchronized);
    CHECK(message.airborne_position.cpr.odd);
    CHECK(message.airborne_position.cpr.latitude_cpr == 0x15555);
    CHECK(message.airborne_position.cpr.longitude_cpr == 0x0AAAA);
}

TEST_CASE("DO-260B 2.2.3.2.3.1: TYPE 9 to 18 carry a pressure altitude and 20 to 22 a GNSS height" *
          doctest::test_suite("main")) {
    for (uint8_t type_code = 9; type_code <= 18; type_code++) {
        const Message message = heard_ok(airborne(type_code, q_field(1440)));
        CHECK(message.airborne_position.pressure_altitude_valid);
        CHECK_FALSE(message.airborne_position.gnss_height_valid);
    }
    for (uint8_t type_code = 20; type_code <= 22; type_code++) {
        const Message message = heard_ok(airborne(type_code, q_field(1440)));
        CHECK(message.airborne_position.gnss_height_valid);
        CHECK_FALSE(message.airborne_position.pressure_altitude_valid);
    }
}

TEST_CASE("DO-260B 2.2.3.2.3.1.3: a TYPE 0 message is never read as a position") {
    CHECK(hear(airborne(0, q_field(1440))).status == Status::Unsupported);
}

TEST_CASE(
    "DO-260B 2.2.3.2.3.1.3.1: TYPE 0 means no position, so its zero CPR fields place nothing" *
    doctest::test_suite("main")) {
    adsb::Squitter frame = squitter(0);
    CHECK(hear(frame).status == Status::Unsupported);
    put(frame, 9, 20, q_field(1440));
    CHECK(hear(frame).status == Status::Unsupported);
}

TEST_CASE("DO-260B 2.2.3.2.3.4: the altitude is the 12 bits of ME 9 to 20") {
    adsb::Squitter frame = airborne(11, 0);
    put(frame, 9, 20, q_field(40));
    CHECK(heard_ok(frame).airborne_position.pressure_altitude_ft == 0);
    put(frame, 8, 8, 1);
    put(frame, 21, 21, 0);
    CHECK(heard_ok(frame).airborne_position.pressure_altitude_ft == 0);
}

TEST_CASE("DO-260B 2.2.3.2.3.4.1: TYPE 9 to 18 report the pressure altitude") {
    const Message message = heard_ok(airborne(13, q_field(1440)));
    CHECK(message.airborne_position.pressure_altitude_valid);
    CHECK(message.airborne_position.pressure_altitude_ft == 35000);
}

TEST_CASE("DO-260B 2.2.3.2.3.4.2: TYPE 20 to 22 report the GNSS height coded as an altitude") {
    const Message q = heard_ok(airborne(20, q_field(1440)));
    CHECK(q.airborne_position.gnss_height_valid);
    CHECK(q.airborne_position.gnss_height_ft == 35000);
    const Message gillham = heard_ok(airborne(21, gillham_field(62000)));
    CHECK(gillham.airborne_position.gnss_height_ft == 62000);
}

TEST_CASE("DO-260B 2.2.3.2.3.4.3: Q 1 counts 25 ft from -1000, Q 0 is Gillham, zeros are none") {
    CHECK(heard_ok(airborne(11, q_field(0))).airborne_position.pressure_altitude_ft == -1000);
    CHECK(heard_ok(airborne(11, q_field(2047))).airborne_position.pressure_altitude_ft == 50175);
    for (int32_t feet : {-1000, -100, 0, 7800, 50200, 62000, 126700}) {
        const Message message = heard_ok(airborne(11, gillham_field(feet)));
        CHECK(message.airborne_position.pressure_altitude_valid);
        CHECK(message.airborne_position.pressure_altitude_ft == feet);
    }
    CHECK_FALSE(heard_ok(airborne(11, 0)).airborne_position.pressure_altitude_valid);
}

TEST_CASE("DO-260B 2.2.3.2.3.5: the T bit says the position is on an exact 0.2 s UTC epoch") {
    adsb::Squitter frame = airborne(11, q_field(1440));
    CHECK(heard_ok(frame).airborne_position.utc_synchronized);
    put(frame, 21, 21, 0);
    CHECK_FALSE(heard_ok(frame).airborne_position.utc_synchronized);
}

TEST_CASE("DO-260B 2.2.3.2.3.6: the F bit is 0 for an even encoding and 1 for an odd one") {
    adsb::Squitter frame = airborne(11, q_field(1440));
    CHECK(heard_ok(frame).airborne_position.cpr.odd);
    put(frame, 22, 22, 0);
    CHECK_FALSE(heard_ok(frame).airborne_position.cpr.odd);
}

TEST_CASE("DO-260B 2.2.3.2.3.7: the encoded latitude is the 17 bits of ME 23 to 39") {
    adsb::Squitter frame = airborne(11, q_field(1440));
    put(frame, 23, 39, 0x1FFFF);
    CHECK(heard_ok(frame).airborne_position.cpr.latitude_cpr == 0x1FFFF);
    put(frame, 23, 39, 0x00001);
    CHECK(heard_ok(frame).airborne_position.cpr.latitude_cpr == 0x00001);
    CHECK(heard_ok(frame).airborne_position.cpr.longitude_cpr == 0x0AAAA);
}

TEST_CASE("DO-260B 2.2.3.2.3.8: the encoded longitude is the 17 bits of ME 40 to 56") {
    adsb::Squitter frame = airborne(11, q_field(1440));
    put(frame, 40, 56, 0x1FFFF);
    CHECK(heard_ok(frame).airborne_position.cpr.longitude_cpr == 0x1FFFF);
    put(frame, 40, 56, 0x10000);
    CHECK(heard_ok(frame).airborne_position.cpr.longitude_cpr == 0x10000);
    CHECK(heard_ok(frame).airborne_position.cpr.latitude_cpr == 0x15555);
}

TEST_CASE("DO-260B 2.2.3.2.4: a surface position is TYPE, movement, track, T, F and CPR" *
          doctest::test_suite("main")) {
    const Message message = heard_ok(surface(6, 39));
    CHECK(message.kind == Kind::SurfacePosition);
    CHECK(message.surface_position.speed_mkt.value == 15000);
    CHECK(message.surface_position.track_valid);
    CHECK(message.surface_position.track_c7 == 64);
    CHECK(message.surface_position.utc_synchronized);
    CHECK_FALSE(message.surface_position.cpr.odd);
    CHECK(message.surface_position.cpr.latitude_cpr == 0x12345);
    CHECK(message.surface_position.cpr.longitude_cpr == 0x0F0F0);
}

TEST_CASE("DO-260B 2.2.3.2.4.1: TYPE 5 to 8 are the surface position") {
    for (uint8_t type_code = 5; type_code <= 8; type_code++)
        CHECK(heard_ok(surface(type_code, 10)).kind == Kind::SurfacePosition);
}

TEST_CASE("DO-260B 2.2.3.2.4.1.3: a TYPE 0 message is never read as a surface position") {
    CHECK(hear(surface(0, 10)).status == Status::Unsupported);
}

TEST_CASE("DO-260B 2.2.3.2.4.1.3.1: TYPE 0 means no position, whatever its movement says") {
    adsb::Squitter frame = squitter(0);
    put(frame, 6, 12, 1);
    CHECK(hear(frame).status == Status::Unsupported);
}

TEST_CASE("DO-260B 2.2.3.2.4.2: movement codes 0, 1 and 9 to 127 read Table 2-18's bands") {
    const auto speed = [](uint8_t code) { return heard_ok(surface(5, code)).surface_position; };
    CHECK_FALSE(speed(0).speed_mkt.valid);
    CHECK(speed(1).speed_mkt.valid);
    CHECK(speed(1).speed_mkt.value == 0);
    CHECK(speed(9).speed_mkt.value == 1000);
    CHECK(speed(13).speed_mkt.value == 2000);
    CHECK(speed(39).speed_mkt.value == 15000);
    CHECK(speed(94).speed_mkt.value == 70000);
    CHECK(speed(109).speed_mkt.value == 100000);
    CHECK(speed(123).speed_mkt.value == 170000);
    CHECK(speed(124).speed_mkt.value == 175000);
    CHECK(speed(124).speed_mkt.saturated);
    for (uint8_t reserved : {125, 126, 127}) CHECK_FALSE(speed(reserved).speed_mkt.valid);
}

// TODO: fc 17nov26 codes 2 to 8 follow Doc 9871's steps, until the published table settles it
TEST_CASE("DO-260B 2.2.3.2.4.2: movement code 2 is under 0.125 kt and 3 to 8 step 0.2700833 km/h" *
          doctest::skip()) {
    const auto floor_mkt = [](uint8_t code) {
        return heard_ok(surface(5, code)).surface_position.speed_mkt.value;
    };
    CHECK(floor_mkt(2) == 0);
    CHECK(floor_mkt(3) == 125);
    CHECK(floor_mkt(8) == 854);
    FAIL("not implemented: codes 2 to 8 read 0.125 to 0.875 kt in 0.125 kt steps");
}

TEST_CASE("DO-260B 2.2.3.2.4.3: the status bit says whether the heading or track is valid") {
    adsb::Squitter frame = surface(7, 10);
    CHECK(heard_ok(frame).surface_position.track_valid);
    put(frame, 13, 13, 0);
    CHECK_FALSE(heard_ok(frame).surface_position.track_valid);
}

TEST_CASE("DO-260B 2.2.3.2.4.4: the heading or track counts 360/128 degree steps from north") {
    adsb::Squitter frame = surface(7, 10);
    for (uint8_t code : {0, 1, 64, 127}) {
        put(frame, 14, 20, code);
        CHECK(heard_ok(frame).surface_position.track_c7 == code);
    }
}

TEST_CASE("DO-260B 2.2.3.2.4.5: the surface T bit says the position is on a 0.2 s UTC epoch") {
    adsb::Squitter frame = surface(5, 10);
    CHECK(heard_ok(frame).surface_position.utc_synchronized);
    put(frame, 21, 21, 0);
    CHECK_FALSE(heard_ok(frame).surface_position.utc_synchronized);
}

TEST_CASE("DO-260B 2.2.3.2.4.6: the surface F bit is 0 even and 1 odd") {
    adsb::Squitter frame = surface(5, 10);
    CHECK_FALSE(heard_ok(frame).surface_position.cpr.odd);
    put(frame, 22, 22, 1);
    CHECK(heard_ok(frame).surface_position.cpr.odd);
}

TEST_CASE("DO-260B 2.2.3.2.4.7: the surface encoded latitude is the 17 bits of ME 23 to 39") {
    adsb::Squitter frame = surface(5, 10);
    put(frame, 23, 39, 0x1FFFF);
    CHECK(heard_ok(frame).surface_position.cpr.latitude_cpr == 0x1FFFF);
    CHECK(heard_ok(frame).surface_position.cpr.longitude_cpr == 0x0F0F0);
}

TEST_CASE("DO-260B 2.2.3.2.4.8: the surface encoded longitude is the 17 bits of ME 40 to 56") {
    adsb::Squitter frame = surface(5, 10);
    put(frame, 40, 56, 0x1FFFF);
    CHECK(heard_ok(frame).surface_position.cpr.longitude_cpr == 0x1FFFF);
    CHECK(heard_ok(frame).surface_position.cpr.latitude_cpr == 0x12345);
}

TEST_CASE("DO-260B 2.2.3.2.5: an identification is TYPE, emitter category and eight characters" *
          doctest::test_suite("main")) {
    const Message message = heard_ok(identification(4, 3, "KLM1023"));
    CHECK(message.kind == Kind::Identification);
    CHECK(message.identification.category_set == 'A');
    CHECK(message.identification.category == 3);
    CHECK(std::strcmp(message.identification.callsign, "KLM1023") == 0);
}

TEST_CASE("DO-260B 2.2.3.2.5.1: TYPE 4, 3, 2 and 1 name category sets A, B, C and D") {
    CHECK(heard_ok(identification(4, 1, "A")).identification.category_set == 'A');
    CHECK(heard_ok(identification(3, 1, "A")).identification.category_set == 'B');
    CHECK(heard_ok(identification(2, 1, "A")).identification.category_set == 'C');
    CHECK(heard_ok(identification(1, 1, "A")).identification.category_set == 'D');
}

TEST_CASE("DO-260B 2.2.3.2.5.2: the emitter category is ME bits 6 to 8, zero for none") {
    for (uint8_t category = 0; category < 8; category++)
        CHECK(heard_ok(identification(3, category, "G")).identification.category == category);
}

TEST_CASE("DO-260B 2.2.3.2.5.3: eight 6-bit IA-5 characters, A to Z, 0 to 9 and space") {
    const Message message = heard_ok(identification(4, 0, "AZ09 X"));
    CHECK(message.identification.callsign_valid);
    CHECK(std::strcmp(message.identification.callsign, "AZ09 X") == 0);
    adsb::Squitter frame = identification(4, 0, "ABCDEFGH");
    put(frame, 9, 14, 0);
    CHECK_FALSE(heard_ok(frame).identification.callsign_valid);
}
