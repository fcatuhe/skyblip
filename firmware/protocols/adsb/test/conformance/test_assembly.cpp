// DO-260B 2.2.10: report assembly, from the first message of an aircraft to its forgetting.
#include <cstdint>

#include "doctest/doctest.h"
#include "protocols/adsb/assembly.h"
#include "protocols/adsb/squitter.h"
#include "protocols/adsb/test/receiver.h"

using namespace skyblip;
using namespace skyblip::adsb;
using namespace skyblip::adsb::receiver;

namespace {

constexpr double kNmDegrees = 1852 / 111320.0;

Bravo tracking() {
    Bravo bravo;
    bravo.fly(kLatitude, kLongitude, 0, false);
    bravo.fly(kLatitude, kLongitude, 500, true);
    REQUIRE(bravo.heard.position_valid);
    return bravo;
}

adsb::Squitter surface(double latitude, bool odd, uint8_t movement = 9) {
    return adsb::surface_position(kAddress, kNoCapability, latitude, kHomeLongitude, odd, movement);
}

Bravo on_ground() {
    Bravo bravo;
    REQUIRE(bravo.hear(surface(kHomeLatitude, false), 0));
    REQUIRE(bravo.hear(surface(kHomeLatitude, true), 500));
    REQUIRE(bravo.heard.position_valid);
    return bravo;
}

}

TEST_CASE("DO-260B 2.2.10: messages become one track per address, and silence ends it") {
    Bravo bravo = tracking();
    CHECK(bravo.assembly.count() == 1);
    bravo.assembly.age_out(500 + kAdsbAircraftForgetMs + 1);
    CHECK(bravo.assembly.count() == 0);
}

TEST_CASE("DO-260B 2.2.10.1: messages fold into a track by the address they came from") {
    Bravo bravo = tracking();
    bravo.fly(kLatitude + 0.5, kLongitude, 600, false, 3000, 0x3C6586);
    CHECK(bravo.assembly.count() == 2);
    CHECK(bravo.heard.address == 0x3C6586);
}

// INFO: fc 17nov26 a figure of the data flow, with nothing a receiver does beyond its subclauses
TEST_CASE("DO-260B 2.2.10.1.1: messages flow from reception to assembly to the reports" *
          doctest::skip()) {
    FAIL("prose: Figure 2-19's data flow, executed by the subclauses that follow it");
}

TEST_CASE("DO-260B 2.2.10.1.2: receptions and reports are indexed by the participant address") {
    Bravo bravo;
    REQUIRE(bravo.hear(adsb::identification(kAddress, kNoCapability, 4, 1, "A"), 0));
    REQUIRE(bravo.hear(adsb::identification(0x3C6586, kNoCapability, 4, 2, "B"), 100));
    CHECK(bravo.heard.address == 0x3C6586);
    CHECK(bravo.heard.emitter_category == 2);
    REQUIRE(bravo.hear(adsb::operational_status(kAddress, kNoCapability, 2, 1), 200));
    CHECK(bravo.heard.address == kAddress);
    CHECK(bravo.heard.emitter_category == 1);
}

TEST_CASE("DO-260B 2.2.10.1.3: a record is kept 200 s and forgotten after 250 s of silence") {
    Bravo bravo;
    REQUIRE(bravo.hear(adsb::identification(kAddress, kNoCapability, 4, 1, "A"), 0));
    bravo.assembly.age_out(200000);
    CHECK(bravo.assembly.count() == 1);
    REQUIRE(bravo.hear(adsb::operational_status(kAddress, kNoCapability, 2, 1), 200000));
    CHECK(bravo.heard.identified);
    bravo.assembly.age_out(200000 + kAdsbAircraftForgetMs + 1);
    CHECK(bravo.assembly.count() == 0);
}

TEST_CASE("DO-260B 2.2.10.1.4: one participant has one track file") {
    Bravo bravo = tracking();
    REQUIRE(bravo.hear(adsb::identification(kAddress, kNoCapability, 4, 1, "A"), 600));
    REQUIRE(bravo.hear(adsb::ground_velocity(kAddress, kNoCapability, 100, 0, 0, 0), 700));
    CHECK(bravo.assembly.count() == 1);
    CHECK(bravo.heard.position_valid);
    CHECK(bravo.heard.identified);
}

TEST_CASE("DO-260B 2.2.10.2: any of the six messages opens a record for an unknown participant") {
    const adsb::Squitter openers[] = {
        adsb::airborne_position(kAddress, kNoCapability, kLatitude, kLongitude, false, 3000),
        adsb::surface_position(kAddress, kNoCapability, kHomeLatitude, kHomeLongitude, false),
        adsb::identification(kAddress, kNoCapability, 4, 1, "A"),
        adsb::ground_velocity(kAddress, kNoCapability, 100, 0, 0, 0),
        adsb::operational_status(kAddress, kNoCapability, 2, 1),
    };
    for (const adsb::Squitter& frame : openers) {
        Bravo bravo;
        REQUIRE(bravo.hear(frame, 0));
        CHECK(bravo.assembly.count() == 1);
    }
}

TEST_CASE("DO-260B 2.2.10.3: an even and an odd position acquire the participant") {
    Bravo bravo;
    bravo.fly(kLatitude, kLongitude, 0, false);
    CHECK_FALSE(bravo.heard.position_valid);
    bravo.fly(kLatitude, kLongitude, 500, true);
    CHECK(bravo.heard.position_valid);
}

TEST_CASE(
    "DO-260B 2.2.10.3.1: an airborne pair within 10 s decodes, unless past the receiver's range" *
    doctest::test_suite("main")) {
    Bravo bravo;
    bravo.fly(kLatitude, kLongitude, 0, false);
    bravo.fly(kLatitude, kLongitude, kAirbornePairMaxAgeMs, true);
    CHECK(bravo.heard.position_valid);
    Bravo late;
    late.fly(kLatitude, kLongitude, 0, false);
    late.fly(kLatitude, kLongitude, kAirbornePairMaxAgeMs + 1, true);
    CHECK_FALSE(late.heard.position_valid);
    Bravo far;
    far.fly(kHomeLatitude + 181 * kNmDegrees, kHomeLongitude, 0, false);
    far.fly(kHomeLatitude + 181 * kNmDegrees, kHomeLongitude, 500, true);
    CHECK_FALSE(far.heard.position_valid);
}

TEST_CASE("DO-260B 2.2.10.3.1.1: the track is handed out by the fold that completes the pair") {
    Bravo bravo;
    bravo.fly(kLatitude, kLongitude, 0, false);
    bravo.fly(kLatitude, kLongitude, 500, true);
    CHECK(bravo.heard.position_valid);
    CHECK(bravo.heard.at_ms == 500);
}

TEST_CASE("DO-260B 2.2.10.3.2: a surface pair within 50 s, or 25 s when fast or unknown, decodes" *
          doctest::test_suite("main")) {
    Bravo slow;
    REQUIRE(slow.hear(surface(kHomeLatitude, false, 9), 0));
    REQUIRE(slow.hear(surface(kHomeLatitude, true, 9), kSlowSurfacePairMaxAgeMs));
    CHECK(slow.heard.position_valid);
    Bravo fast;
    REQUIRE(fast.hear(surface(kHomeLatitude, false, 49), 0));
    REQUIRE(fast.hear(surface(kHomeLatitude, true, 49), kSurfacePairMaxAgeMs + 1));
    CHECK_FALSE(fast.heard.position_valid);
    Bravo unknown;
    REQUIRE(unknown.hear(surface(kHomeLatitude, false, 0), 0));
    REQUIRE(unknown.hear(surface(kHomeLatitude, true, 0), kSurfacePairMaxAgeMs + 1));
    CHECK_FALSE(unknown.heard.position_valid);
}

TEST_CASE(
    "DO-260B 2.2.10.3.2.1: the surface track is handed out by the fold that completes the pair") {
    Bravo bravo = on_ground();
    CHECK(bravo.heard.at_ms == 500);
    CHECK(bravo.heard.air_ground == AirGround::Ground);
}

TEST_CASE("DO-260B 2.2.10.3.3: a message before the pair is kept and shows once the track does") {
    Bravo bravo;
    REQUIRE(bravo.hear(adsb::identification(kAddress, kNoCapability, 3, 1, "DGLID"), 0));
    REQUIRE(bravo.hear(adsb::operational_status(kAddress, kNoCapability, 2, 1), 100));
    bravo.fly(kLatitude, kLongitude, 200, false);
    bravo.fly(kLatitude, kLongitude, 700, true);
    CHECK(bravo.heard.position_valid);
    CHECK(bravo.heard.identified);
    CHECK(bravo.heard.emitter_category == 1);
    CHECK(bravo.heard.version == 2);
}

TEST_CASE("DO-260B 2.2.10.4: a tracked participant moves on single positions") {
    Bravo bravo = tracking();
    bravo.fly(kLatitude + 0.01, kLongitude, 1500, false);
    CHECK(metres_from(bravo.heard, kLatitude + 0.01, kLongitude) < 5.1);
}

TEST_CASE("DO-260B 2.2.10.4.1: an airborne track takes position and velocity messages") {
    Bravo bravo = tracking();
    REQUIRE(bravo.hear(adsb::ground_velocity(kAddress, kNoCapability, 120, 0, 0, 0), 900));
    bravo.fly(kLatitude, kLongitude + 0.01, 1500, false);
    CHECK(bravo.heard.motion.east_kt == 120);
    CHECK(metres_from(bravo.heard, kLatitude, kLongitude + 0.01) < 5.1);
}

TEST_CASE("DO-260B 2.2.10.4.1.1: a velocity after acquisition completes the airborne track") {
    Bravo bravo = tracking();
    REQUIRE(bravo.hear(adsb::ground_velocity(kAddress, kNoCapability, 120, -30, 640, 50), 900));
    CHECK(bravo.heard.position_valid);
    CHECK(bravo.heard.motion.source == MotionSource::Velocity);
    CHECK(bravo.heard.climb_fpm == 640);
}

TEST_CASE("DO-260B 2.2.10.4.1.2: a single airborne position moves the track 6 NM at most in 30 s") {
    Bravo near = tracking();
    near.fly(kLatitude + 5.9 * kNmDegrees, kLongitude, 1500, false);
    CHECK(metres_from(near.heard, kLatitude + 5.9 * kNmDegrees, kLongitude) < 5.1);
    Bravo jumped = tracking();
    jumped.fly(kLatitude + 6.5 * kNmDegrees, kLongitude, 1500, false);
    CHECK(metres_from(jumped.heard, kLatitude, kLongitude) < 5.1);
    CHECK(jumped.assembly.implausible() == 1);
}

// TODO: fc 17nov26 the owner keeps 6 s airborne on Bravo and the Go, until he revisits 25 s
TEST_CASE("DO-260B 2.2.10.4.1.3: an airborne track silent for 25 s, +/-5, is terminated" *
          doctest::skip()) {
    Bravo bravo = tracking();
    REQUIRE(bravo.hear(adsb::identification(kAddress, kNoCapability, 4, 1, "A"), 500 + 30001));
    CHECK_FALSE(bravo.heard.position_valid);
    FAIL(
        "not implemented: the traffic table forgets the target after 6 s, the owner's choice, and "
        "the assembly keeps its position 120 s for its own pairing");
}

TEST_CASE("DO-260B 2.2.10.4.2: a surface track takes surface positions") {
    Bravo bravo = on_ground();
    REQUIRE(bravo.hear(surface(kHomeLatitude + 0.001, false), 2000));
    CHECK(metres_from(bravo.heard, kHomeLatitude + 0.001, kHomeLongitude) < 1.3);
}

TEST_CASE("DO-260B 2.2.10.4.2.1: the surface track starts with the pair that acquired it") {
    Bravo bravo;
    REQUIRE(bravo.hear(adsb::identification(kAddress, 4, 2, 1, "FIRE1"), 0));
    REQUIRE(bravo.hear(surface(kHomeLatitude, false), 100));
    REQUIRE(bravo.hear(surface(kHomeLatitude, true), 600));
    CHECK(bravo.heard.position_valid);
    CHECK(bravo.heard.identified);
    CHECK(bravo.heard.emitter_type_code == 2);
}

TEST_CASE(
    "DO-260B 2.2.10.4.2.2: a single surface position moves the track 0.75 NM at most in 30 s") {
    Bravo near = on_ground();
    REQUIRE(near.hear(surface(kHomeLatitude + 0.7 * kNmDegrees, false), 2000));
    CHECK(metres_from(near.heard, kHomeLatitude + 0.7 * kNmDegrees, kHomeLongitude) < 1.3);
    Bravo jumped = on_ground();
    REQUIRE(jumped.hear(surface(kHomeLatitude + 0.8 * kNmDegrees, false), 2000));
    CHECK(metres_from(jumped.heard, kHomeLatitude, kHomeLongitude) < 1.3);
    CHECK(jumped.assembly.implausible() == 1);
}

// TODO: fc 17nov26 the owner keeps 60 s on the ground on Bravo and the Go, until he revisits 200 s
TEST_CASE("DO-260B 2.2.10.4.2.3: a surface track is terminated after 200 s, +/-5, of silence" *
          doctest::skip()) {
    Bravo bravo = on_ground();
    REQUIRE(bravo.hear(adsb::identification(kAddress, 4, 2, 1, "A"), 500 + 190000));
    CHECK(bravo.heard.position_valid);
    FAIL(
        "not implemented: the traffic table forgets the target after 60 s on the ground, the "
        "owner's choice, and the assembly clears its position at 120 s");
}

TEST_CASE("DO-260B 2.2.10.5: a class A0 receiver keeps 100 track files, and drops the farthest") {
    CHECK(Assembly::kMaxAircraft == 100);
    Bravo bravo;
    for (int i = 0; i < 101; i++) {
        Message message;
        const double latitude = kHomeLatitude + (i + 1) * 0.009;
        REQUIRE(receive_sealed(adsb::airborne_position(0x400000u + static_cast<uint32_t>(i), 5,
                                                       latitude, kHomeLongitude, false, 3000),
                               message) == wire::Status::Ok);
        bravo.assembly.fold(message, static_cast<uint32_t>(i), Receipt{}, bravo.reference,
                            bravo.heard);
    }
    CHECK(bravo.assembly.count() == 100);
    CHECK(bravo.assembly.refusals() == 1);
}

TEST_CASE("DO-260B 2.2.10.6: global and local decodes are each checked before they are believed") {
    Bravo bravo = tracking();
    bravo.fly(kLatitude + 7 * kNmDegrees, kLongitude, 1500, false);
    CHECK(bravo.assembly.implausible() == 1);
}

TEST_CASE("DO-260B 2.2.10.6.1: a decode that fails its test is counted and placed nowhere") {
    Bravo bravo;
    bravo.fly(kHomeLatitude + 3.1, kHomeLongitude, 0, false);
    bravo.fly(kHomeLatitude + 3.1, kHomeLongitude, 500, true);
    CHECK_FALSE(bravo.heard.position_valid);
    CHECK(bravo.assembly.implausible() == 1);
}

TEST_CASE(
    "DO-260B 2.2.10.6.2: a second pair agrees with the local decode to 5 m, or the track goes") {
    Bravo honest;
    for (uint32_t at_ms = 0; at_ms <= 1500; at_ms += 500)
        honest.fly(kLatitude, kLongitude, at_ms, (at_ms / 500) % 2 == 1);
    CHECK(honest.heard.position_valid);
    CHECK(honest.assembly.implausible() == 0);
    Bravo bad;
    bad.reference = Reference{};
    bad.fly(kLatitude, kLongitude, 0, false);
    bad.fly(kLatitude + 0.2, kLongitude, 500, true);
    REQUIRE(bad.heard.position_valid);
    bad.fly(kLatitude, kLongitude, 1000, false);
    bad.fly(kLatitude, kLongitude, 1500, true);
    CHECK_FALSE(bad.heard.position_valid);
}

TEST_CASE("DO-260B 2.2.10.6.3: past 30 s a single position is not believed, and a new pair is") {
    Bravo bravo = tracking();
    bravo.fly(kLatitude + 0.1, kLongitude, 500 + kTrackJumpWindowMs + 1, false);
    CHECK(bravo.heard.at_ms == 500);
    bravo.fly(kLatitude + 0.1, kLongitude, 500 + kTrackJumpWindowMs + 500, true);
    CHECK(metres_from(bravo.heard, kLatitude + 0.1, kLongitude) < 5.1);
}

// TODO: fc 17nov26 one record per address, until a duplicate address is seen often enough to matter
TEST_CASE("DO-260B 2.2.10.7: two aircraft sending one address are told apart" * doctest::skip()) {
    FAIL("not implemented: a second aircraft with a tracked address only counts as implausible");
}

// TODO: fc 17nov26 one record per address, until a duplicate address is seen often enough to matter
TEST_CASE("DO-260B 2.2.10.7.1: a local decode that fails opens a candidate duplicate record" *
          doctest::skip()) {
    FAIL("not implemented: the failed position is counted and dropped, and no candidate is opened");
}

// TODO: fc 17nov26 one record per address, until a duplicate address is seen often enough to matter
TEST_CASE(
    "DO-260B 2.2.10.7.2: a candidate's own pair declares the duplicate and flags both reports" *
    doctest::skip()) {
    FAIL("not implemented: no duplicate address flag is reported");
}
