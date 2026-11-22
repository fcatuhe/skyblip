// DO-260B 2.2.8 and 2.2.9: the reports a track carries, read as the Track the assembly hands out.
#include <cstdint>
#include <cstring>

#include "doctest/doctest.h"
#include "protocols/adsb/assembly.h"
#include "protocols/adsb/squitter.h"
#include "protocols/adsb/test/receiver.h"

using namespace skyblip;
using namespace skyblip::adsb;
using namespace skyblip::adsb::receiver;

namespace {

Bravo tracking(uint32_t at_ms = 1000) {
    Bravo bravo;
    bravo.fly(kLatitude, kLongitude, at_ms - 1000, false, 3000);
    bravo.fly(kLatitude, kLongitude, at_ms, true, 3000);
    REQUIRE(bravo.heard.position_valid);
    return bravo;
}

bool only_df17_read() {
    adsb::Squitter frame = adsb::operational_status(kAddress, kNoCapability, 2, 1);
    frame.bytes[0] = static_cast<uint8_t>(18 << 3);
    Message message;
    return receive_sealed(frame, message) == wire::Status::Unsupported;
}

adsb::Squitter air_velocity(int32_t east_kt, int32_t north_kt, int32_t climb_fpm, bool barometric) {
    adsb::Squitter frame =
        adsb::ground_velocity(kAddress, kNoCapability, east_kt, north_kt, climb_fpm, 75);
    adsb::set_message_bits(frame, 36, 36, barometric ? 1 : 0);
    return frame;
}

adsb::Squitter surface_at(bool odd, uint8_t movement, bool track_valid, uint8_t track) {
    adsb::Squitter frame = adsb::surface_position(kAddress, kNoCapability, kHomeLatitude,
                                                  kHomeLongitude, odd, movement);
    adsb::set_message_bits(frame, 13, 13, track_valid ? 1 : 0);
    adsb::set_message_bits(frame, 14, 20, track);
    return frame;
}

}

TEST_CASE("DO-260B 2.2.8: a Track carries the report parameters in a structure of its own") {
    Bravo bravo = tracking();
    REQUIRE(bravo.hear(air_velocity(100, -50, 640, false), 1500));
    REQUIRE(bravo.hear(adsb::identification(kAddress, kNoCapability, 4, 1, "DLH4AB"), 1600));
    REQUIRE(bravo.hear(adsb::operational_status(kAddress, kNoCapability, 2, 1), 1700));
    const Track& track = bravo.heard;
    CHECK(track.address == kAddress);
    CHECK(track.position_valid);
    CHECK(track.pressure_altitude_valid);
    CHECK(track.motion.source == MotionSource::Velocity);
    CHECK(track.climb_valid);
    CHECK(track.identified);
    CHECK(track.version_valid);
}

TEST_CASE("DO-260B 2.2.8.1: the state vector is position, altitudes, velocity and climb" *
          doctest::test_suite("main")) {
    Bravo bravo = tracking();
    REQUIRE(bravo.hear(air_velocity(100, -50, 640, false), 1500));
    CHECK(metres_from(bravo.heard, kLatitude, kLongitude) < 10);
    CHECK(bravo.heard.pressure_altitude_ft == 3000);
    CHECK(bravo.heard.motion.east_kt == 100);
    CHECK(bravo.heard.motion.north_kt == -50);
    CHECK(bravo.heard.climb_fpm == 640);
}

TEST_CASE("DO-260B 2.2.8.1.1: every state vector element the Track carries has its validity") {
    Bravo bravo;
    REQUIRE(bravo.hear(adsb::identification(kAddress, kNoCapability, 4, 1, "DLH4AB"), 0));
    CHECK_FALSE(bravo.heard.position_valid);
    CHECK_FALSE(bravo.heard.pressure_altitude_valid);
    CHECK_FALSE(bravo.heard.gnss_height_valid);
    CHECK_FALSE(bravo.heard.climb_valid);
    CHECK(bravo.heard.motion.source == MotionSource::None);
}

TEST_CASE("DO-260B 2.2.8.1.2: the participant address is the AA field") {
    const uint32_t other = 0x3C6586;
    Bravo bravo;
    REQUIRE(bravo.hear(adsb::identification(other, kNoCapability, 4, 1, "A"), 0));
    CHECK(bravo.heard.address == other);
}

TEST_CASE("DO-260B 2.2.8.1.3: every Track is from DF17, whose AA is an ICAO address") {
    CHECK(only_df17_read());
    Bravo bravo = tracking();
    CHECK(bravo.heard.address == kAddress);
}

TEST_CASE("DO-260B 2.2.8.1.4: position and velocity each keep the instant they were true" *
          doctest::test_suite("main")) {
    Bravo bravo = tracking(1000);
    REQUIRE(bravo.hear(air_velocity(100, -50, 640, false), 1400));
    CHECK(bravo.heard.at_ms == 1000);
    CHECK(bravo.heard.motion_age_ms == -400);
    CHECK(bravo.heard.climb_age_ms == -400);
}

TEST_CASE("DO-260B 2.2.8.1.4.2: the position's instant is set by the message that placed it") {
    Bravo bravo = tracking(1000);
    bravo.fly(kLatitude, kLongitude, 1800, false, 3000);
    CHECK(bravo.heard.at_ms == 1800);
    REQUIRE(bravo.hear(adsb::identification(kAddress, kNoCapability, 4, 1, "A"), 2500));
    CHECK(bravo.heard.at_ms == 1800);
}

TEST_CASE("DO-260B 2.2.8.1.4.2.1: with T 0 the position is as of the message's receipt") {
    Bravo bravo = tracking(1000);
    CHECK(bravo.heard.at_ms == 1000);
    CHECK(bravo.heard.received.utc_s == kUtcS + 1);
    CHECK(bravo.heard.received.utc_ms == 0);
}

TEST_CASE("DO-260B 2.2.8.1.4.2.3: with T 1 and no UTC the position is as of its receipt") {
    Bravo bravo;
    adsb::Squitter even =
        adsb::airborne_position(kAddress, kNoCapability, kLatitude, kLongitude, false, 3000);
    adsb::Squitter odd =
        adsb::airborne_position(kAddress, kNoCapability, kLatitude, kLongitude, true, 3000);
    adsb::set_message_bits(even, 21, 21, 1);
    adsb::set_message_bits(odd, 21, 21, 1);
    Message message;
    REQUIRE(receive_sealed(even, message) == wire::Status::Ok);
    REQUIRE(bravo.assembly.fold(message, 400, Receipt{}, bravo.reference, bravo.heard));
    REQUIRE(receive_sealed(odd, message) == wire::Status::Ok);
    REQUIRE(bravo.assembly.fold(message, 1300, Receipt{}, bravo.reference, bravo.heard));
    REQUIRE(bravo.heard.position_valid);
    CHECK(bravo.heard.at_ms == 1300);
}

TEST_CASE(
    "DO-260B 2.2.8.1.4.3: the velocity is as of the velocity or surface message that gave it") {
    Bravo bravo = tracking(1000);
    REQUIRE(bravo.hear(air_velocity(100, -50, 640, false), 1300));
    CHECK(bravo.heard.motion_age_ms == -300);
    Bravo ground;
    REQUIRE(ground.hear(surface_at(false, 20, true, 10), 0));
    REQUIRE(ground.hear(surface_at(true, 20, true, 10), 700));
    CHECK(ground.heard.motion_age_ms == 0);
}

TEST_CASE("DO-260B 2.2.8.1.5: the latitude is the CPR decode, and none is no latitude") {
    Bravo bravo = tracking();
    CHECK(bravo.heard.position.latitude_1e7 == doctest::Approx(e7(kLatitude)).epsilon(1e-6));
    Bravo half;
    half.fly(kLatitude, kLongitude, 0, false);
    CHECK_FALSE(half.heard.position_valid);
    CHECK(half.heard.position.latitude_1e7 == 0);
}

TEST_CASE("DO-260B 2.2.8.1.6: the longitude is the CPR decode, and none is no longitude") {
    Bravo bravo = tracking();
    CHECK(bravo.heard.position.longitude_1e7 == doctest::Approx(e7(kLongitude)).epsilon(1e-6));
    Bravo half;
    half.fly(kLatitude, kLongitude, 0, true);
    CHECK_FALSE(half.heard.position_valid);
    CHECK(half.heard.position.longitude_1e7 == 0);
}

TEST_CASE(
    "DO-260B 2.2.8.1.7: the geometric altitude comes from TYPE 20 to 22 or baro plus difference" *
    doctest::test_suite("main")) {
    Bravo bravo = tracking();
    REQUIRE(bravo.hear(air_velocity(100, -50, 640, false), 1500));
    CHECK(bravo.heard.gnss_height_valid);
    CHECK(bravo.heard.gnss_height_ft == 3075);
    Bravo direct;
    REQUIRE(direct.hear(
        adsb::airborne_position(kAddress, kNoCapability, kLatitude, kLongitude, false, 5000, true),
        0));
    CHECK(direct.heard.gnss_height_valid);
    CHECK(direct.heard.gnss_height_ft == 5000);
}

TEST_CASE("DO-260B 2.2.8.1.8: the north/south velocity is signed by its direction bit") {
    Bravo bravo = tracking();
    REQUIRE(bravo.hear(air_velocity(0, -321, 0, false), 1500));
    CHECK(bravo.heard.motion.north_kt == -321);
}

TEST_CASE("DO-260B 2.2.8.1.9: the east/west velocity is signed by its direction bit") {
    Bravo bravo = tracking();
    REQUIRE(bravo.hear(air_velocity(-123, 0, 0, false), 1500));
    CHECK(bravo.heard.motion.east_kt == -123);
}

TEST_CASE("DO-260B 2.2.8.1.10: the surface ground speed is the movement code's") {
    Bravo ground;
    REQUIRE(ground.hear(surface_at(false, 39, true, 32), 0));
    CHECK(ground.heard.motion.source == MotionSource::Surface);
    CHECK(ground.heard.motion.speed_mkt == 15000);
    Bravo stopped;
    REQUIRE(stopped.hear(surface_at(false, 1, false, 0), 0));
    CHECK(stopped.heard.motion.speed_mkt == 0);
}

// TODO: fc 17nov26 Motion holds speed and track together, until a bare ground speed has a use
TEST_CASE("DO-260B 2.2.8.1.10: a ground speed is reported when the heading is not valid" *
          doctest::skip()) {
    Bravo ground;
    REQUIRE(ground.hear(surface_at(false, 39, false, 0), 0));
    CHECK(ground.heard.motion.source == MotionSource::Surface);
    FAIL("not implemented: a moving surface message with no valid heading reports no ground speed");
}

TEST_CASE("DO-260B 2.2.8.1.11: the surface heading is reported only with its status bit") {
    Bravo ground;
    REQUIRE(ground.hear(surface_at(false, 39, true, 32), 0));
    CHECK(ground.heard.motion.track_valid);
    CHECK(ground.heard.motion.track_c7 == 32);
    Bravo stopped;
    REQUIRE(stopped.hear(surface_at(false, 1, false, 32), 0));
    CHECK_FALSE(stopped.heard.motion.track_valid);
}

TEST_CASE(
    "DO-260B 2.2.8.1.12: the pressure altitude comes from TYPE 9 to 18, and none is invalid") {
    Bravo bravo = tracking();
    CHECK(bravo.heard.pressure_altitude_valid);
    CHECK(bravo.heard.pressure_altitude_ft == 3000);
    Bravo none;
    adsb::Squitter frame =
        adsb::airborne_position(kAddress, kNoCapability, kLatitude, kLongitude, false, 3000);
    adsb::set_message_bits(frame, 9, 20, 0);
    REQUIRE(none.hear(frame, 0));
    CHECK_FALSE(none.heard.pressure_altitude_valid);
}

TEST_CASE("DO-260B 2.2.8.1.13: the vertical rate is named baro or geometric by its source" *
          doctest::test_suite("main")) {
    Bravo bravo = tracking();
    REQUIRE(bravo.hear(air_velocity(10, 10, -1280, true), 1500));
    CHECK(bravo.heard.climb_fpm == -1280);
    CHECK(bravo.heard.climb_source == HeightSource::Pressure);
}

TEST_CASE("DO-260B 2.2.8.1.14: a geometric source bit gives the geometric vertical rate") {
    Bravo bravo = tracking();
    REQUIRE(bravo.hear(air_velocity(10, 10, 1280, false), 1500));
    CHECK(bravo.heard.climb_valid);
    CHECK(bravo.heard.climb_source == HeightSource::Gnss);
    CHECK(bravo.heard.climb_fpm == 1280);
}

TEST_CASE("DO-260B 2.2.8.1.15: a barometric source bit gives the barometric altitude rate") {
    Bravo bravo = tracking();
    REQUIRE(bravo.hear(air_velocity(10, 10, 1280, true), 1500));
    CHECK(bravo.heard.climb_valid);
    CHECK(bravo.heard.climb_source == HeightSource::Pressure);
    CHECK(bravo.heard.climb_fpm == 1280);
}

TEST_CASE("DO-260B 2.2.8.2: the mode status is version, call sign, category and GVA") {
    Bravo bravo;
    REQUIRE(bravo.hear(adsb::identification(kAddress, kNoCapability, 3, 1, "DGLID"), 0));
    REQUIRE(bravo.hear(adsb::operational_status(kAddress, kNoCapability, 2, 1), 100));
    CHECK(bravo.heard.identified);
    CHECK(bravo.heard.emitter_type_code == 3);
    CHECK(bravo.heard.emitter_category == 1);
    CHECK(bravo.heard.version == 2);
    CHECK(bravo.heard.gva == 1);
}

TEST_CASE("DO-260B 2.2.8.2.1: the mode status elements carry their validity") {
    Bravo bravo;
    REQUIRE(bravo.hear(
        adsb::airborne_position(kAddress, kNoCapability, kLatitude, kLongitude, false, 3000), 0));
    CHECK_FALSE(bravo.heard.identified);
    CHECK_FALSE(bravo.heard.version_valid);
    CHECK_FALSE(bravo.heard.gva_valid);
}

TEST_CASE("DO-260B 2.2.8.2.2: the mode status names the AA field's address") {
    Bravo bravo;
    REQUIRE(bravo.hear(adsb::operational_status(0x3C6586, kNoCapability, 2, 1), 0));
    CHECK(bravo.heard.address == 0x3C6586);
}

TEST_CASE("DO-260B 2.2.8.2.3: the mode status address is ICAO, as every DF17 AA is") {
    CHECK(only_df17_read());
    Bravo bravo;
    REQUIRE(bravo.hear(adsb::operational_status(kAddress, kNoCapability, 2, 1), 0));
    CHECK(bravo.heard.address == kAddress);
}

TEST_CASE("DO-260B 2.2.8.2.4: a mode status update keeps the instant of the message that made it") {
    Bravo bravo;
    REQUIRE(bravo.hear(adsb::operational_status(kAddress, kNoCapability, 2, 1), 700));
    CHECK(bravo.heard.at_ms == 700);
}

TEST_CASE("DO-260B 2.2.8.2.5: the version comes from the operational status, zeros until heard") {
    Bravo bravo;
    REQUIRE(bravo.hear(adsb::identification(kAddress, kNoCapability, 4, 1, "A"), 0));
    CHECK_FALSE(bravo.heard.version_valid);
    CHECK(bravo.heard.version == 0);
    REQUIRE(bravo.hear(adsb::operational_status(kAddress, kNoCapability, 1, 0), 100));
    CHECK(bravo.heard.version_valid);
    CHECK(bravo.heard.version == 1);
}

TEST_CASE("DO-260B 2.2.8.2.7: the emitter category is the TYPE code's set and the category") {
    Bravo bravo;
    REQUIRE(bravo.hear(adsb::identification(kAddress, kNoCapability, 4, 7, "A"), 0));
    CHECK(bravo.heard.emitter_type_code == 4);
    CHECK(bravo.heard.emitter_category == 7);
    REQUIRE(bravo.hear(adsb::identification(kAddress, kNoCapability, 3, 4, "A"), 100));
    CHECK(bravo.heard.emitter_type_code == 3);
    CHECK(bravo.heard.emitter_category == 4);
}

TEST_CASE("DO-260B 2.2.8.2.15: the GVA comes from the operational status") {
    Bravo bravo;
    REQUIRE(bravo.hear(adsb::operational_status(kAddress, kNoCapability, 2, 1), 0));
    CHECK(bravo.heard.gva_valid);
    CHECK(bravo.heard.gva == 1);
}

TEST_CASE("DO-260B 2.2.8.2.18: the vertical rate type is the velocity's source bit") {
    Bravo bravo = tracking();
    REQUIRE(bravo.hear(air_velocity(10, 10, 640, true), 1500));
    CHECK(bravo.heard.climb_source == HeightSource::Pressure);
    REQUIRE(bravo.hear(air_velocity(10, 10, 640, false), 1600));
    CHECK(bravo.heard.climb_source == HeightSource::Gnss);
}

TEST_CASE("DO-260B 2.2.8.4: a reserved TYPE code between two positions changes nothing") {
    Bravo bravo;
    bravo.fly(kLatitude, kLongitude, 0, false, 3000);
    for (uint8_t type_code : {0, 23, 24, 25, 26, 27, 28, 29, 30}) {
        Message message;
        adsb::Squitter frame = adsb::squitter(kAddress, kNoCapability, type_code);
        CHECK(receive_sealed(frame, message) == wire::Status::Unsupported);
    }
    bravo.fly(kLatitude, kLongitude, 1000, true, 3000);
    CHECK(bravo.heard.position_valid);
    CHECK(metres_from(bravo.heard, kLatitude, kLongitude) < 10);
}

TEST_CASE("DO-260B 2.2.9: the Track is the report set of the least class, A0") {
    CHECK(Assembly::kMaxAircraft == 100);
    Bravo bravo = tracking();
    CHECK(bravo.heard.position_valid);
}

TEST_CASE(
    "DO-260B 2.2.9.1: a class A0 receiver reports state vector and mode status, airborne or not" *
    doctest::test_suite("main")) {
    Bravo bravo = tracking();
    REQUIRE(bravo.hear(adsb::operational_status(kAddress, kNoCapability, 2, 1), 1500));
    CHECK(bravo.heard.position_valid);
    CHECK(bravo.heard.version_valid);
    Bravo ground;
    REQUIRE(ground.hear(surface_at(false, 20, true, 10), 0));
    REQUIRE(ground.hear(surface_at(true, 20, true, 10), 700));
    CHECK(ground.heard.position_valid);
    CHECK(ground.heard.air_ground == AirGround::Ground);
}

TEST_CASE("DO-260B 2.2.9.1.1: every class A receiver gives state vectors") {
    Bravo bravo = tracking();
    CHECK(bravo.heard.position_valid);
    CHECK(bravo.heard.pressure_altitude_valid);
}

TEST_CASE("DO-260B 2.2.9.1.2: every class A receiver gives mode status") {
    Bravo bravo;
    REQUIRE(bravo.hear(adsb::identification(kAddress, kNoCapability, 4, 1, "A"), 0));
    REQUIRE(bravo.hear(adsb::operational_status(kAddress, kNoCapability, 2, 1), 100));
    CHECK(bravo.heard.identified);
    CHECK(bravo.heard.version_valid);
}
