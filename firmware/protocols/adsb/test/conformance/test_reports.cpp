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

AdsbFrame air_velocity(int32_t east_kt, int32_t north_kt, int32_t climb_fpm, bool barometric) {
    AdsbFrame frame =
        adsb_ground_velocity(kAddress, kNoCapability, east_kt, north_kt, climb_fpm, 75);
    adsb_set_message_bits(frame, 36, 36, barometric ? 1 : 0);
    return frame;
}

AdsbFrame surface_at(bool odd, uint8_t movement, bool track_valid, uint8_t track) {
    AdsbFrame frame = adsb_surface_position(kAddress, kNoCapability, kHomeLatitude, kHomeLongitude,
                                            odd, movement);
    adsb_set_message_bits(frame, 13, 13, track_valid ? 1 : 0);
    adsb_set_message_bits(frame, 14, 20, track);
    return frame;
}

}

TEST_CASE("DO-260B 2.2.8: a Track carries the report parameters in a structure of its own") {
    Bravo bravo = tracking();
    REQUIRE(bravo.hear(air_velocity(100, -50, 640, false), 1500));
    REQUIRE(bravo.hear(adsb_identification(kAddress, kNoCapability, 4, 1, "DLH4AB"), 1600));
    REQUIRE(bravo.hear(adsb_operational_status(kAddress, kNoCapability, 2, 1), 1700));
    const Track& track = bravo.heard;
    CHECK(track.address == kAddress);
    CHECK(track.position_valid);
    CHECK(track.pressure_altitude_valid);
    CHECK(track.motion.source == MotionSource::Velocity);
    CHECK(track.climb_valid);
    CHECK(track.identified);
    CHECK(track.version_valid);
}

TEST_CASE("DO-260B 2.2.8.1: the state vector is position, altitudes, velocity and climb") {
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
    REQUIRE(bravo.hear(adsb_identification(kAddress, kNoCapability, 4, 1, "DLH4AB"), 0));
    CHECK_FALSE(bravo.heard.position_valid);
    CHECK_FALSE(bravo.heard.pressure_altitude_valid);
    CHECK_FALSE(bravo.heard.gnss_height_valid);
    CHECK_FALSE(bravo.heard.climb_valid);
    CHECK(bravo.heard.motion.source == MotionSource::None);
}

// INFO: fc 17nov26 2.2.8.1 binds the byte layout only to Table 2-81's sample structure
TEST_CASE("DO-260B 2.2.8.1.1.1: the report type is the high nibble of byte 0" * doctest::skip()) {
    FAIL(
        "optional: the byte layout binds only equipment that uses Table 2-81's sample structure, "
        "and the Track is not that");
}

// INFO: fc 17nov26 2.2.8.1 binds the byte layout only to Table 2-81's sample structure
TEST_CASE("DO-260B 2.2.8.1.1.2: the validity flags are bytes 3 and 4" * doctest::skip()) {
    FAIL(
        "optional: the byte layout binds only equipment that uses Table 2-81's sample structure, "
        "and each Track field has its own flag");
}

TEST_CASE("DO-260B 2.2.8.1.2: the participant address is the AA field") {
    const uint32_t other = 0x3C6586;
    Bravo bravo;
    REQUIRE(bravo.hear(adsb_identification(other, kNoCapability, 4, 1, "A"), 0));
    CHECK(bravo.heard.address == other);
}

TEST_CASE("DO-260B 2.2.8.1.3: every Track is from DF17, whose AA is an ICAO address") {
    CHECK(kSyncChips == 0x95);
    Bravo bravo = tracking();
    CHECK(bravo.heard.address == kAddress);
}

TEST_CASE("DO-260B 2.2.8.1.4: position and velocity each keep the instant they were true") {
    Bravo bravo = tracking(1000);
    REQUIRE(bravo.hear(air_velocity(100, -50, 640, false), 1400));
    CHECK(bravo.heard.at_ms == 1000);
    CHECK(bravo.heard.motion_age_ms == -400);
    CHECK(bravo.heard.climb_age_ms == -400);
}

// TODO: fc 17nov26 the assembly estimates nothing, until a consumer asks it to extrapolate
TEST_CASE(
    "DO-260B 2.2.8.1.4.1: an estimated position or velocity carries the instant it was estimated "
    "for" *
    doctest::skip()) {
    FAIL("not implemented: the Track carries what was heard and estimates nothing");
}

TEST_CASE("DO-260B 2.2.8.1.4.2: the position's instant is set by the message that placed it") {
    Bravo bravo = tracking(1000);
    bravo.fly(kLatitude, kLongitude, 1800, false, 3000);
    CHECK(bravo.heard.at_ms == 1800);
    REQUIRE(bravo.hear(adsb_identification(kAddress, kNoCapability, 4, 1, "A"), 2500));
    CHECK(bravo.heard.at_ms == 1800);
}

TEST_CASE("DO-260B 2.2.8.1.4.2.1: with T 0 the position is as of the message's receipt") {
    Bravo bravo = tracking(1000);
    CHECK(bravo.heard.at_ms == 1000);
    CHECK(bravo.heard.received.utc_s == kUtcS + 1);
    CHECK(bravo.heard.received.utc_ms == 0);
}

// TODO: fc 17nov26 the T bit is unused, until a consumer needs better than the receipt's instant
TEST_CASE(
    "DO-260B 2.2.8.1.4.2.2: with T 1 and UTC the position is as of the nearest 0.2 s epoch its F "
    "names" *
    doctest::skip()) {
    FAIL(
        "not implemented: a T 1 position keeps the instant of its receipt, not the 0.2 s UTC "
        "epoch");
}

TEST_CASE("DO-260B 2.2.8.1.4.2.3: with T 1 and no UTC the position is as of its receipt") {
    Bravo bravo;
    AdsbFrame even =
        adsb_airborne_position(kAddress, kNoCapability, kLatitude, kLongitude, false, 3000);
    AdsbFrame odd =
        adsb_airborne_position(kAddress, kNoCapability, kLatitude, kLongitude, true, 3000);
    adsb_set_message_bits(even, 21, 21, 1);
    adsb_set_message_bits(odd, 21, 21, 1);
    Message message;
    REQUIRE(adsb_hear(even, message) == wire::Status::Ok);
    REQUIRE(bravo.assembly.fold(message, 400, Receipt{}, bravo.reference, bravo.heard));
    REQUIRE(adsb_hear(odd, message) == wire::Status::Ok);
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
    "DO-260B 2.2.8.1.7: the geometric altitude comes from TYPE 20 to 22 or baro plus difference") {
    Bravo bravo = tracking();
    REQUIRE(bravo.hear(air_velocity(100, -50, 640, false), 1500));
    CHECK(bravo.heard.gnss_height_valid);
    CHECK(bravo.heard.gnss_height_ft == 3075);
    Bravo direct;
    REQUIRE(direct.hear(
        adsb_airborne_position(kAddress, kNoCapability, kLatitude, kLongitude, false, 5000, true),
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
    AdsbFrame frame =
        adsb_airborne_position(kAddress, kNoCapability, kLatitude, kLongitude, false, 3000);
    adsb_set_message_bits(frame, 9, 20, 0);
    REQUIRE(none.hear(frame, 0));
    CHECK_FALSE(none.heard.pressure_altitude_valid);
}

TEST_CASE("DO-260B 2.2.8.1.13: the vertical rate is named baro or geometric by its source") {
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

// TODO: fc 17nov26 no NIC is reported, until a consumer weighs a position's integrity
TEST_CASE("DO-260B 2.2.8.1.16: the NIC comes from the TYPE code and the NIC supplements" *
          doctest::skip()) {
    FAIL("not implemented: no NIC is reported");
}

// TODO: fc 17nov26 the assembly estimates nothing, until a consumer asks it to extrapolate
TEST_CASE("DO-260B 2.2.8.1.17: the estimated latitude follows the latest position and velocity" *
          doctest::skip()) {
    FAIL("not implemented: the Track carries what was heard and estimates nothing");
}

// TODO: fc 17nov26 the assembly estimates nothing, until a consumer asks it to extrapolate
TEST_CASE("DO-260B 2.2.8.1.18: the estimated longitude follows the latest position and velocity" *
          doctest::skip()) {
    FAIL("not implemented: the Track carries what was heard and estimates nothing");
}

// INFO: fc 17nov26 its note makes velocity estimation optional, and the assembly does not estimate
TEST_CASE("DO-260B 2.2.8.1.19: the estimated north/south velocity follows the latest velocity" *
          doctest::skip()) {
    FAIL("optional: the clause's note leaves velocity estimation to the implementer");
}

// INFO: fc 17nov26 its note makes velocity estimation optional, and the assembly does not estimate
TEST_CASE("DO-260B 2.2.8.1.20: the estimated east/west velocity follows the latest velocity" *
          doctest::skip()) {
    FAIL("optional: the clause's note leaves velocity estimation to the implementer");
}

// TODO: fc 17nov26 neither field is read, until the glass can show an emergency or a SPI
TEST_CASE("DO-260B 2.2.8.1.21: the surveillance status and intent change flag ride in the report" *
          doctest::skip()) {
    FAIL("not implemented: neither the surveillance status nor the intent change flag is read");
}

// INFO: fc 17nov26 Table 2-101 lists the report mode as not required
TEST_CASE("DO-260B 2.2.8.1.22: the report mode says acquisition or track" * doctest::skip()) {
    FAIL("optional: Table 2-101 does not require the report mode, and the Track has none");
}

TEST_CASE("DO-260B 2.2.8.2: the mode status is version, call sign, category and GVA") {
    Bravo bravo;
    REQUIRE(bravo.hear(adsb_identification(kAddress, kNoCapability, 3, 1, "DGLID"), 0));
    REQUIRE(bravo.hear(adsb_operational_status(kAddress, kNoCapability, 2, 1), 100));
    CHECK(bravo.heard.identified);
    CHECK(bravo.heard.emitter_type_code == 3);
    CHECK(bravo.heard.emitter_category == 1);
    CHECK(bravo.heard.version == 2);
    CHECK(bravo.heard.gva == 1);
}

TEST_CASE("DO-260B 2.2.8.2.1: the mode status elements carry their validity") {
    Bravo bravo;
    REQUIRE(bravo.hear(
        adsb_airborne_position(kAddress, kNoCapability, kLatitude, kLongitude, false, 3000), 0));
    CHECK_FALSE(bravo.heard.identified);
    CHECK_FALSE(bravo.heard.version_valid);
    CHECK_FALSE(bravo.heard.gva_valid);
}

// INFO: fc 17nov26 2.2.8.2 binds the byte layout only to Table 2-88's sample structure
TEST_CASE("DO-260B 2.2.8.2.1.1: the mode status report type and structure are its first bytes" *
          doctest::skip()) {
    FAIL("optional: the byte layout binds only equipment that uses Table 2-88's sample structure");
}

// INFO: fc 17nov26 2.2.8.2 binds the byte layout only to Table 2-88's sample structure
TEST_CASE("DO-260B 2.2.8.2.1.2: the mode status validity flags are byte 3" * doctest::skip()) {
    FAIL(
        "optional: the byte layout binds only equipment that uses Table 2-88's sample structure, "
        "and each Track field has its own flag");
}

TEST_CASE("DO-260B 2.2.8.2.2: the mode status names the AA field's address") {
    Bravo bravo;
    REQUIRE(bravo.hear(adsb_operational_status(0x3C6586, kNoCapability, 2, 1), 0));
    CHECK(bravo.heard.address == 0x3C6586);
}

TEST_CASE("DO-260B 2.2.8.2.3: the mode status address is ICAO, as every DF17 AA is") {
    CHECK(kSyncChips == 0x95);
    Bravo bravo;
    REQUIRE(bravo.hear(adsb_operational_status(kAddress, kNoCapability, 2, 1), 0));
    CHECK(bravo.heard.address == kAddress);
}

TEST_CASE("DO-260B 2.2.8.2.4: a mode status update keeps the instant of the message that made it") {
    Bravo bravo;
    REQUIRE(bravo.hear(adsb_operational_status(kAddress, kNoCapability, 2, 1), 700));
    CHECK(bravo.heard.at_ms == 700);
}

TEST_CASE("DO-260B 2.2.8.2.5: the version comes from the operational status, zeros until heard") {
    Bravo bravo;
    REQUIRE(bravo.hear(adsb_identification(kAddress, kNoCapability, 4, 1, "A"), 0));
    CHECK_FALSE(bravo.heard.version_valid);
    CHECK(bravo.heard.version == 0);
    REQUIRE(bravo.hear(adsb_operational_status(kAddress, kNoCapability, 1, 0), 100));
    CHECK(bravo.heard.version_valid);
    CHECK(bravo.heard.version == 1);
}

// TODO: fc 17nov26 the call sign stays on the Message, until a consumer of the Track needs it there
TEST_CASE("DO-260B 2.2.8.2.6: the call sign is the eight identification characters as IA-5" *
          doctest::skip()) {
    FAIL(
        "not implemented: the Track says it was identified but does not carry the call sign, which "
        "Bravo reads off the Message");
}

TEST_CASE("DO-260B 2.2.8.2.7: the emitter category is the TYPE code's set and the category") {
    Bravo bravo;
    REQUIRE(bravo.hear(adsb_identification(kAddress, kNoCapability, 4, 7, "A"), 0));
    CHECK(bravo.heard.emitter_type_code == 4);
    CHECK(bravo.heard.emitter_category == 7);
    REQUIRE(bravo.hear(adsb_identification(kAddress, kNoCapability, 3, 4, "A"), 100));
    CHECK(bravo.heard.emitter_type_code == 3);
    CHECK(bravo.heard.emitter_category == 4);
}

// TODO: fc 17nov26 no size is reported, until a surface picture draws one
TEST_CASE("DO-260B 2.2.8.2.8: the length and width code sizes a surface participant" *
          doctest::skip()) {
    FAIL("not implemented: the operational status's length and width code is not read");
}

// TODO: fc 17nov26 TYPE 28 is not decoded, until the glass can show an emergency
TEST_CASE("DO-260B 2.2.8.2.9: the emergency/priority status comes from TYPE 28" * doctest::skip()) {
    FAIL("not implemented: TYPE 28 is not decoded");
}

// TODO: fc 17nov26 no capability class is reported, until an application asks what a sender can do
TEST_CASE("DO-260B 2.2.8.2.10: the capability class codes come from the operational status" *
          doctest::skip()) {
    FAIL("not implemented: the capability class is not read");
}

// TODO: fc 17nov26 no operational mode is reported, until an application shows an IDENT or an RA
TEST_CASE("DO-260B 2.2.8.2.11: the operational mode comes from the operational status" *
          doctest::skip()) {
    FAIL("not implemented: the operational mode is not read");
}

// TODO: fc 17nov26 no NACp is reported, until a consumer weighs a position's accuracy
TEST_CASE("DO-260B 2.2.8.2.12: the NACp comes from the operational status" * doctest::skip()) {
    FAIL("not implemented: the NACp is not read");
}

// TODO: fc 17nov26 no NACv is reported, until a consumer weighs a velocity's accuracy
TEST_CASE("DO-260B 2.2.8.2.13: the NACv comes from the velocity" * doctest::skip()) {
    FAIL("not implemented: the NACv is not read");
}

// TODO: fc 17nov26 no integrity level is reported, until a consumer weighs a position's integrity
TEST_CASE(
    "DO-260B 2.2.8.2.14: the SIL, its supplement and the SDA come from the operational status" *
    doctest::skip()) {
    FAIL("not implemented: none of the three is read");
}

// TODO: fc 17nov26 no SIL is reported, until a consumer weighs a position's integrity
TEST_CASE("DO-260B 2.2.8.2.14.1: the SIL comes from the operational status" * doctest::skip()) {
    FAIL("not implemented: the SIL is not read");
}

// TODO: fc 17nov26 no SIL is reported, until a consumer weighs a position's integrity
TEST_CASE("DO-260B 2.2.8.2.14.2: the SIL supplement comes from the operational status" *
          doctest::skip()) {
    FAIL("not implemented: the SIL supplement is not read");
}

// TODO: fc 17nov26 no SDA is reported, until a consumer weighs a source's design assurance
TEST_CASE("DO-260B 2.2.8.2.14.3: the SDA comes from the operational mode" * doctest::skip()) {
    FAIL("not implemented: the SDA is not read");
}

TEST_CASE("DO-260B 2.2.8.2.15: the GVA comes from the operational status") {
    Bravo bravo;
    REQUIRE(bravo.hear(adsb_operational_status(kAddress, kNoCapability, 2, 1), 0));
    CHECK(bravo.heard.gva_valid);
    CHECK(bravo.heard.gva == 1);
}

// TODO: fc 17nov26 no NICbaro is reported, until a consumer weighs an altitude's integrity
TEST_CASE("DO-260B 2.2.8.2.16: the NICbaro comes from the operational status" * doctest::skip()) {
    FAIL("not implemented: the NICbaro is not read");
}

// TODO: fc 17nov26 no heading reference is reported, until an airspeed heading is drawn as a track
TEST_CASE("DO-260B 2.2.8.2.17: the track/heading bit and the HRD come from the operational status" *
          doctest::skip()) {
    FAIL("not implemented: neither bit is read");
}

TEST_CASE("DO-260B 2.2.8.2.18: the vertical rate type is the velocity's source bit") {
    Bravo bravo = tracking();
    REQUIRE(bravo.hear(air_velocity(10, 10, 640, true), 1500));
    CHECK(bravo.heard.climb_source == HeightSource::Pressure);
    REQUIRE(bravo.hear(air_velocity(10, 10, 640, false), 1600));
    CHECK(bravo.heard.climb_source == HeightSource::Gnss);
}

// INFO: fc 17nov26 Table 2-102 lists the reserved field as not required
TEST_CASE("DO-260B 2.2.8.2.19: the reserved flight mode data is all zeros" * doctest::skip()) {
    FAIL("optional: Table 2-102 does not require the reserved field, and the Track has none");
}

// INFO: fc 17nov26 2.2.9.1.3 and 2.2.9.1.4 require neither report of class A0
TEST_CASE(
    "DO-260B 2.2.8.3: the on-condition reports are the target state and the air-referenced "
    "velocity" *
    doctest::skip()) {
    FAIL(
        "optional: class A0 is required to give neither on-condition report, and Bravo gives none");
}

// INFO: fc 17nov26 2.2.9.1.3 requires it only of classes A2 and A3
TEST_CASE(
    "DO-260B 2.2.8.3.1: the target state report gives the selected altitude, heading and modes" *
    doctest::skip()) {
    FAIL(
        "optional: 2.2.9.1.3 does not require it of class A0, the class Bravo's 100 track files "
        "match");
}

// INFO: fc 17nov26 2.2.9.1.4 requires it only of classes A1S to A3
TEST_CASE("DO-260B 2.2.8.3.2: the air-referenced velocity report gives airspeed and heading" *
          doctest::skip()) {
    FAIL(
        "optional: 2.2.9.1.4 does not require it of class A0, the class Bravo's 100 track files "
        "match");
}

TEST_CASE("DO-260B 2.2.8.4: a reserved TYPE code between two positions changes nothing") {
    Bravo bravo;
    bravo.fly(kLatitude, kLongitude, 0, false, 3000);
    for (uint8_t type_code : {0, 23, 24, 25, 26, 27, 28, 29, 30}) {
        Message message;
        AdsbFrame frame = adsb_frame(kAddress, kNoCapability, type_code);
        CHECK(adsb_hear(frame, message) == wire::Status::Unsupported);
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
    "DO-260B 2.2.9.1: a class A0 receiver reports state vector and mode status, airborne or not") {
    Bravo bravo = tracking();
    REQUIRE(bravo.hear(adsb_operational_status(kAddress, kNoCapability, 2, 1), 1500));
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
    REQUIRE(bravo.hear(adsb_identification(kAddress, kNoCapability, 4, 1, "A"), 0));
    REQUIRE(bravo.hear(adsb_operational_status(kAddress, kNoCapability, 2, 1), 100));
    CHECK(bravo.heard.identified);
    CHECK(bravo.heard.version_valid);
}

// INFO: fc 17nov26 its item a exempts class A0
TEST_CASE("DO-260B 2.2.9.1.3: classes A2 and A3 give target state reports" * doctest::skip()) {
    FAIL("optional: class A0 is not required to give target state reports");
}

// INFO: fc 17nov26 its item a exempts class A0
TEST_CASE("DO-260B 2.2.9.1.4: classes A1S to A3 give air-referenced velocity reports" *
          doctest::skip()) {
    FAIL("optional: class A0 is not required to give air-referenced velocity reports");
}

// INFO: fc 17nov26 a transmit-only class, with nothing a receiver does
TEST_CASE("DO-260B 2.2.9.2: class B, broadcast only, has no report" * doctest::skip()) {
    FAIL("prose: there is nothing a receiver does for class B");
}
