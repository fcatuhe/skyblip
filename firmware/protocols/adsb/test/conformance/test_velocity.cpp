// DO-260B 2.2.3.2.6: the airborne velocity, subtype by subtype and subfield by subfield.
#include <cstdint>
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

Velocity heard_velocity(const adsb::Squitter& frame) { return heard_ok(frame).velocity; }

int32_t step_kt(uint8_t subtype) { return subtype == 2 || subtype == 4 ? 4 : 1; }

void check_subtype(uint8_t subtype) {
    const Velocity read = heard_velocity(velocity(subtype));
    CHECK(read.subtype == subtype);
    if (subtype <= 2) {
        CHECK(read.east_kt.value == 100 * step_kt(subtype));
        CHECK(read.north_kt.value == -200 * step_kt(subtype));
    } else {
        CHECK(read.heading_valid);
        CHECK(read.heading_c10 == 256);
        CHECK(read.airspeed_kt.value == 300 * step_kt(subtype));
    }
    CHECK(read.climb_fpm.value == -640);
    CHECK(read.gnss_minus_pressure_ft.value == 100);
}

void check_type_code(uint8_t subtype) {
    CHECK(heard_ok(velocity(subtype)).type_code == 19);
    adsb::Squitter other = velocity(subtype);
    put(other, 1, 5, 18);
    CHECK(heard_ok(other).kind == Kind::AirbornePosition);
}

void check_subtype_code(uint8_t subtype) {
    CHECK(heard_velocity(velocity(subtype)).subtype == subtype);
    adsb::Squitter reserved = velocity(subtype);
    put(reserved, 6, 8, 0);
    CHECK(hear(reserved).status == Status::Unsupported);
}

void check_ignored(uint8_t subtype, uint8_t first, uint8_t last) {
    const Velocity plain = heard_velocity(velocity(subtype));
    adsb::Squitter set = velocity(subtype);
    put(set, first, last, (1u << (last - first + 1)) - 1);
    const Velocity read = heard_velocity(set);
    CHECK(read.east_kt.value == plain.east_kt.value);
    CHECK(read.north_kt.value == plain.north_kt.value);
    CHECK(read.airspeed_kt.value == plain.airspeed_kt.value);
    CHECK(read.heading_c10 == plain.heading_c10);
    CHECK(read.climb_fpm.value == plain.climb_fpm.value);
    CHECK(read.gnss_minus_pressure_ft.value == plain.gnss_minus_pressure_ft.value);
}

void check_east_sign(uint8_t subtype) {
    adsb::Squitter frame = velocity(subtype);
    CHECK(heard_velocity(frame).east_kt.value > 0);
    put(frame, 14, 14, 1);
    CHECK(heard_velocity(frame).east_kt.value < 0);
}

void check_north_sign(uint8_t subtype) {
    adsb::Squitter frame = velocity(subtype);
    CHECK(heard_velocity(frame).north_kt.value < 0);
    put(frame, 25, 25, 0);
    CHECK(heard_velocity(frame).north_kt.value > 0);
}

Quantity speed_component(uint8_t subtype, uint8_t first, uint16_t code) {
    adsb::Squitter frame = velocity(subtype);
    put(frame, first, static_cast<uint8_t>(first + 9), code);
    if (subtype <= 2) put(frame, 25, 25, 0);
    const Velocity read = heard_velocity(frame);
    if (first == 15) return read.east_kt;
    if (first == 26 && subtype <= 2) return read.north_kt;
    return read.airspeed_kt;
}

void check_speed(uint8_t subtype, uint8_t first) {
    const int32_t step = step_kt(subtype);
    CHECK_FALSE(speed_component(subtype, first, 0).valid);
    CHECK(speed_component(subtype, first, 1).valid);
    CHECK(speed_component(subtype, first, 1).value == 0);
    CHECK(speed_component(subtype, first, 2).value == step);
    CHECK(speed_component(subtype, first, 1022).value == 1021 * step);
    CHECK_FALSE(speed_component(subtype, first, 1022).saturated);
    CHECK(speed_component(subtype, first, 1023).saturated);
}

void check_heading_status(uint8_t subtype) {
    adsb::Squitter frame = velocity(subtype);
    CHECK(heard_velocity(frame).heading_valid);
    put(frame, 14, 14, 0);
    CHECK_FALSE(heard_velocity(frame).heading_valid);
}

void check_heading(uint8_t subtype) {
    adsb::Squitter frame = velocity(subtype);
    for (uint16_t code : {0, 1, 512, 1023}) {
        put(frame, 15, 24, code);
        CHECK(heard_velocity(frame).heading_c10 == code);
    }
}

void check_airspeed_type(uint8_t subtype) {
    adsb::Squitter frame = velocity(subtype);
    CHECK(heard_velocity(frame).airspeed_type == Airspeed::True);
    put(frame, 25, 25, 0);
    CHECK(heard_velocity(frame).airspeed_type == Airspeed::Indicated);
}

void check_climb_source(uint8_t subtype) {
    adsb::Squitter frame = velocity(subtype);
    CHECK(heard_velocity(frame).climb_source == HeightSource::Pressure);
    put(frame, 36, 36, 0);
    CHECK(heard_velocity(frame).climb_source == HeightSource::Gnss);
}

void check_climb_sign(uint8_t subtype) {
    adsb::Squitter frame = velocity(subtype);
    CHECK(heard_velocity(frame).climb_fpm.value == -640);
    put(frame, 37, 37, 0);
    CHECK(heard_velocity(frame).climb_fpm.value == 640);
}

Quantity climb(uint8_t subtype, uint16_t code) {
    adsb::Squitter frame = velocity(subtype);
    put(frame, 37, 37, 0);
    put(frame, 38, 46, code);
    return heard_velocity(frame).climb_fpm;
}

void check_climb(uint8_t subtype) {
    CHECK_FALSE(climb(subtype, 0).valid);
    CHECK(climb(subtype, 1).value == 0);
    CHECK(climb(subtype, 2).value == 64);
    CHECK(climb(subtype, 510).value == 32576);
    CHECK_FALSE(climb(subtype, 510).saturated);
    CHECK(climb(subtype, 511).saturated);
}

void check_difference_sign(uint8_t subtype) {
    adsb::Squitter frame = velocity(subtype);
    CHECK(heard_velocity(frame).gnss_minus_pressure_ft.value == 100);
    put(frame, 49, 49, 1);
    CHECK(heard_velocity(frame).gnss_minus_pressure_ft.value == -100);
}

Quantity difference(uint8_t subtype, uint8_t code) {
    adsb::Squitter frame = velocity(subtype);
    put(frame, 50, 56, code);
    return heard_velocity(frame).gnss_minus_pressure_ft;
}

void check_difference(uint8_t subtype) {
    CHECK_FALSE(difference(subtype, 0).valid);
    CHECK(difference(subtype, 1).value == 0);
    CHECK(difference(subtype, 2).value == 25);
    CHECK(difference(subtype, 126).value == 3125);
    CHECK_FALSE(difference(subtype, 126).saturated);
    CHECK(difference(subtype, 127).saturated);
}

}

TEST_CASE("DO-260B 2.2.3.2.6: TYPE 19 subtypes 1 to 4 are the airborne velocities") {
    for (uint8_t subtype = 1; subtype <= 4; subtype++)
        CHECK(heard_ok(velocity(subtype)).kind == Kind::Velocity);
}

TEST_CASE(
    "DO-260B 2.2.3.2.6.1: subtype 1 reads its ground speed, subsonic in 1 kt steps, climb and "
    "difference") {
    check_subtype(1);
}

TEST_CASE("DO-260B 2.2.3.2.6.1.1: subtype 1 rides on TYPE 19") { check_type_code(1); }

TEST_CASE("DO-260B 2.2.3.2.6.1.2: subtype 1 is ME bits 6 to 8, and subtype 0 is reserved") {
    check_subtype_code(1);
}

// TODO: fc 17nov26 no report carries the intent change, until an application asks for it
TEST_CASE("DO-260B 2.2.3.2.6.1.3: subtype 1's intent change flag says the intent changed" *
          doctest::skip()) {
    FAIL("not implemented: ME bit 9 is not read");
}

TEST_CASE("DO-260B 2.2.3.2.6.1.4: subtype 1's reserved bit A changes nothing read") {
    check_ignored(1, 10, 10);
}

// TODO: fc 17nov26 no NACv is reported, until a consumer weighs a velocity's accuracy
TEST_CASE("DO-260B 2.2.3.2.6.1.5: subtype 1's NACv bounds the velocity's error" * doctest::skip()) {
    FAIL("not implemented: ME bits 11 to 13 are not read, and no NACv is reported");
}

TEST_CASE("DO-260B 2.2.3.2.6.1.6: subtype 1's east/west bit is 0 east and 1 west") {
    check_east_sign(1);
}

TEST_CASE(
    "DO-260B 2.2.3.2.6.1.7: subtype 1's east/west velocity is zeros none, 1 zero and 1023 the "
    "top") {
    check_speed(1, 15);
}

TEST_CASE("DO-260B 2.2.3.2.6.1.8: subtype 1's north/south bit is 0 north and 1 south") {
    check_north_sign(1);
}

TEST_CASE(
    "DO-260B 2.2.3.2.6.1.9: subtype 1's north/south velocity is zeros none, 1 zero and 1023 the "
    "top") {
    check_speed(1, 26);
}

TEST_CASE(
    "DO-260B 2.2.3.2.6.1.10: subtype 1's vertical rate source is 0 geometric and 1 barometric") {
    check_climb_source(1);
}

TEST_CASE("DO-260B 2.2.3.2.6.1.11: subtype 1's vertical rate sign is 0 up and 1 down") {
    check_climb_sign(1);
}

TEST_CASE("DO-260B 2.2.3.2.6.1.12: subtype 1's vertical rate counts 64 ft/min, 511 past 32608") {
    check_climb(1);
}

TEST_CASE("DO-260B 2.2.3.2.6.1.13: subtype 1's reserved bits B change nothing read") {
    check_ignored(1, 47, 48);
}

TEST_CASE(
    "DO-260B 2.2.3.2.6.1.14: subtype 1's difference sign is 0 above and 1 below the pressure "
    "altitude") {
    check_difference_sign(1);
}

TEST_CASE("DO-260B 2.2.3.2.6.1.15: subtype 1's difference counts 25 ft, 127 past 3137.5 ft") {
    check_difference(1);
}

TEST_CASE(
    "DO-260B 2.2.3.2.6.2: subtype 2 reads its ground speed, supersonic in 4 kt steps, climb and "
    "difference") {
    check_subtype(2);
}

TEST_CASE("DO-260B 2.2.3.2.6.2.1: subtype 2 rides on TYPE 19") { check_type_code(2); }

TEST_CASE("DO-260B 2.2.3.2.6.2.2: subtype 2 is ME bits 6 to 8, and subtype 0 is reserved") {
    check_subtype_code(2);
}

// TODO: fc 17nov26 no report carries the intent change, until an application asks for it
TEST_CASE("DO-260B 2.2.3.2.6.2.3: subtype 2's intent change flag says the intent changed" *
          doctest::skip()) {
    FAIL("not implemented: ME bit 9 is not read");
}

TEST_CASE("DO-260B 2.2.3.2.6.2.4: subtype 2's reserved bit A changes nothing read") {
    check_ignored(2, 10, 10);
}

// TODO: fc 17nov26 no NACv is reported, until a consumer weighs a velocity's accuracy
TEST_CASE("DO-260B 2.2.3.2.6.2.5: subtype 2's NACv bounds the velocity's error" * doctest::skip()) {
    FAIL("not implemented: ME bits 11 to 13 are not read, and no NACv is reported");
}

TEST_CASE("DO-260B 2.2.3.2.6.2.6: subtype 2's east/west bit is 0 east and 1 west") {
    check_east_sign(2);
}

TEST_CASE(
    "DO-260B 2.2.3.2.6.2.7: subtype 2's east/west velocity is zeros none, 1 zero and 1023 the "
    "top") {
    check_speed(2, 15);
}

TEST_CASE("DO-260B 2.2.3.2.6.2.8: subtype 2's north/south bit is 0 north and 1 south") {
    check_north_sign(2);
}

TEST_CASE(
    "DO-260B 2.2.3.2.6.2.9: subtype 2's north/south velocity is zeros none, 1 zero and 1023 the "
    "top") {
    check_speed(2, 26);
}

TEST_CASE(
    "DO-260B 2.2.3.2.6.2.10: subtype 2's vertical rate source is 0 geometric and 1 barometric") {
    check_climb_source(2);
}

TEST_CASE("DO-260B 2.2.3.2.6.2.11: subtype 2's vertical rate sign is 0 up and 1 down") {
    check_climb_sign(2);
}

TEST_CASE("DO-260B 2.2.3.2.6.2.12: subtype 2's vertical rate counts 64 ft/min, 511 past 32608") {
    check_climb(2);
}

TEST_CASE("DO-260B 2.2.3.2.6.2.13: subtype 2's reserved bits B change nothing read") {
    check_ignored(2, 47, 48);
}

TEST_CASE(
    "DO-260B 2.2.3.2.6.2.14: subtype 2's difference sign is 0 above and 1 below the pressure "
    "altitude") {
    check_difference_sign(2);
}

TEST_CASE("DO-260B 2.2.3.2.6.2.15: subtype 2's difference counts 25 ft, 127 past 3137.5 ft") {
    check_difference(2);
}

TEST_CASE(
    "DO-260B 2.2.3.2.6.3: subtype 3 reads its airspeed, subsonic in 1 kt steps, climb and "
    "difference") {
    check_subtype(3);
}

TEST_CASE("DO-260B 2.2.3.2.6.3.1: subtype 3 rides on TYPE 19") { check_type_code(3); }

TEST_CASE("DO-260B 2.2.3.2.6.3.2: subtype 3 is ME bits 6 to 8, and subtype 0 is reserved") {
    check_subtype_code(3);
}

// TODO: fc 17nov26 no report carries the intent change, until an application asks for it
TEST_CASE("DO-260B 2.2.3.2.6.3.3: subtype 3's intent change flag says the intent changed" *
          doctest::skip()) {
    FAIL("not implemented: ME bit 9 is not read");
}

TEST_CASE("DO-260B 2.2.3.2.6.3.4: subtype 3's reserved bit A changes nothing read") {
    check_ignored(3, 10, 10);
}

// TODO: fc 17nov26 no NACv is reported, until a consumer weighs a velocity's accuracy
TEST_CASE("DO-260B 2.2.3.2.6.3.5: subtype 3's NACv bounds the velocity's error" * doctest::skip()) {
    FAIL("not implemented: ME bits 11 to 13 are not read, and no NACv is reported");
}

TEST_CASE("DO-260B 2.2.3.2.6.3.6: subtype 3's heading status bit says the heading is valid") {
    check_heading_status(3);
}

TEST_CASE("DO-260B 2.2.3.2.6.3.7: subtype 3's heading counts 360/1024 degree steps") {
    check_heading(3);
}

TEST_CASE("DO-260B 2.2.3.2.6.3.8: subtype 3's airspeed type is 0 indicated and 1 true") {
    check_airspeed_type(3);
}

TEST_CASE("DO-260B 2.2.3.2.6.3.9: subtype 3's airspeed is zeros none, 1 zero and 1023 the top") {
    check_speed(3, 26);
}

TEST_CASE(
    "DO-260B 2.2.3.2.6.3.10: subtype 3's vertical rate source is 0 geometric and 1 barometric") {
    check_climb_source(3);
}

TEST_CASE("DO-260B 2.2.3.2.6.3.11: subtype 3's vertical rate sign is 0 up and 1 down") {
    check_climb_sign(3);
}

TEST_CASE("DO-260B 2.2.3.2.6.3.12: subtype 3's vertical rate counts 64 ft/min, 511 past 32608") {
    check_climb(3);
}

TEST_CASE("DO-260B 2.2.3.2.6.3.13: subtype 3's reserved bits B change nothing read") {
    check_ignored(3, 47, 48);
}

TEST_CASE(
    "DO-260B 2.2.3.2.6.3.14: subtype 3's difference sign is 0 above and 1 below the pressure "
    "altitude") {
    check_difference_sign(3);
}

TEST_CASE("DO-260B 2.2.3.2.6.3.15: subtype 3's difference counts 25 ft, 127 past 3137.5 ft") {
    check_difference(3);
}

TEST_CASE(
    "DO-260B 2.2.3.2.6.4: subtype 4 reads its airspeed, supersonic in 4 kt steps, climb and "
    "difference") {
    check_subtype(4);
}

TEST_CASE("DO-260B 2.2.3.2.6.4.1: subtype 4 rides on TYPE 19") { check_type_code(4); }

TEST_CASE("DO-260B 2.2.3.2.6.4.2: subtype 4 is ME bits 6 to 8, and subtype 0 is reserved") {
    check_subtype_code(4);
}

// TODO: fc 17nov26 no report carries the intent change, until an application asks for it
TEST_CASE("DO-260B 2.2.3.2.6.4.3: subtype 4's intent change flag says the intent changed" *
          doctest::skip()) {
    FAIL("not implemented: ME bit 9 is not read");
}

TEST_CASE("DO-260B 2.2.3.2.6.4.4: subtype 4's reserved bit A changes nothing read") {
    check_ignored(4, 10, 10);
}

// TODO: fc 17nov26 no NACv is reported, until a consumer weighs a velocity's accuracy
TEST_CASE("DO-260B 2.2.3.2.6.4.5: subtype 4's NACv bounds the velocity's error" * doctest::skip()) {
    FAIL("not implemented: ME bits 11 to 13 are not read, and no NACv is reported");
}

TEST_CASE("DO-260B 2.2.3.2.6.4.6: subtype 4's heading status bit says the heading is valid") {
    check_heading_status(4);
}

TEST_CASE("DO-260B 2.2.3.2.6.4.7: subtype 4's heading counts 360/1024 degree steps") {
    check_heading(4);
}

TEST_CASE("DO-260B 2.2.3.2.6.4.8: subtype 4's airspeed type is 0 indicated and 1 true") {
    check_airspeed_type(4);
}

TEST_CASE("DO-260B 2.2.3.2.6.4.9: subtype 4's airspeed is zeros none, 1 zero and 1023 the top") {
    check_speed(4, 26);
}

TEST_CASE(
    "DO-260B 2.2.3.2.6.4.10: subtype 4's vertical rate source is 0 geometric and 1 barometric") {
    check_climb_source(4);
}

TEST_CASE("DO-260B 2.2.3.2.6.4.11: subtype 4's vertical rate sign is 0 up and 1 down") {
    check_climb_sign(4);
}

TEST_CASE("DO-260B 2.2.3.2.6.4.12: subtype 4's vertical rate counts 64 ft/min, 511 past 32608") {
    check_climb(4);
}

TEST_CASE("DO-260B 2.2.3.2.6.4.13: subtype 4's reserved bits B change nothing read") {
    check_ignored(4, 47, 48);
}

TEST_CASE(
    "DO-260B 2.2.3.2.6.4.14: subtype 4's difference sign is 0 above and 1 below the pressure "
    "altitude") {
    check_difference_sign(4);
}

TEST_CASE("DO-260B 2.2.3.2.6.4.15: subtype 4's difference counts 25 ft, 127 past 3137.5 ft") {
    check_difference(4);
}

TEST_CASE("DO-260B 2.2.3.2.6.5: subtypes 5, 6 and 7 are reserved and never read") {
    for (uint8_t subtype : {5, 6, 7}) CHECK(hear(velocity(subtype)).status == Status::Unsupported);
}
