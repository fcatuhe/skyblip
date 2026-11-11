// Each aircraft's DF17 messages folded into one observation, by DO-260B §2.2.10's receiver rules.
#include <cstdint>

#include "core/flight/state.h"
#include "core/model/aircraft.h"
#include "core/protocol/adsb.h"
#include "core/protocol/adsb_assembly.h"
#include "core/units/units.h"
#include "doctest/doctest.h"
#include "test/support/adsb_frames.h"
#include "test/support/adsb_receiver.h"

using namespace skyblip;
using namespace skyblip::protocol;
using namespace skyblip::adsb_receiver;

// A position needs both halves of a pair, and DO-260B A.1.7.7 allows them 10 s apart at most.
TEST_CASE("adsb assembly: an airborne pair decodes up to 10 s apart and not a millisecond more") {
    Bravo bravo;
    bravo.fly(kLatitude, kLongitude, 0, false);
    bravo.fly(kLatitude, kLongitude, kAirbornePairMaxAgeMs, true);
    CHECK(bravo.heard.position_valid);

    Bravo late;
    late.fly(kLatitude, kLongitude, 0, false);
    late.fly(kLatitude, kLongitude, kAirbornePairMaxAgeMs + 1, true);
    CHECK_FALSE(late.heard.position_valid);
    CHECK(late.heard.at_ms == kAirbornePairMaxAgeMs + 1);

    late.fly(kLatitude, kLongitude, kAirbornePairMaxAgeMs + 500, false);
    REQUIRE(late.heard.position_valid);
    CHECK(metres_from(late.heard, kLatitude, kLongitude) < 5.1);
    CHECK(late.heard.at_ms == kAirbornePairMaxAgeMs + 500);
}

TEST_CASE("adsb assembly: an observation is an ICAO ADS-B target with its own address") {
    Bravo bravo;
    bravo.fly(kLatitude, kLongitude, 0, false);
    CHECK(bravo.heard.address == kAddress);
    CHECK(bravo.heard.address_mapping_table == kIcaoAddressMappingTable);
    CHECK(bravo.heard.source == model::Source::Adsb);
}

TEST_CASE("adsb assembly: a callsign or a velocity does not refresh the position they follow") {
    Bravo bravo;
    bravo.fly(kLatitude, kLongitude, 1000, false);
    bravo.fly(kLatitude, kLongitude, 1500, true);
    REQUIRE(bravo.heard.position_valid);
    const events::Stamp received = bravo.heard.received;

    REQUIRE(bravo.hear(adsb_identification(kAddress, kNoCapability, 3, 1, "DGLID"), 4000));
    CHECK(bravo.heard.at_ms == 1500);
    CHECK(bravo.heard.received.at_s == received.at_s);
    CHECK(bravo.heard.received.into_ms == received.into_ms);
    CHECK(bravo.heard.aircraft_category == 4);

    REQUIRE(bravo.hear(adsb_ground_velocity(kAddress, kNoCapability, 0, 80, 0, 0), 4000));
    CHECK(bravo.heard.at_ms == 1500);
    CHECK(bravo.heard.speed_age_ms == -2500);
}

TEST_CASE("adsb assembly: without a position the observation is as recent as its newest message") {
    Bravo bravo;
    REQUIRE(bravo.hear(adsb_ground_velocity(kAddress, kNoCapability, 0, 80, 640, 0), 2000));
    CHECK_FALSE(bravo.heard.position_valid);
    CHECK(bravo.heard.at_ms == 2000);
    CHECK(bravo.heard.climb_age_ms == 0);
    REQUIRE(bravo.hear(adsb_identification(kAddress, kNoCapability, 3, 1, "DGLID"), 3000));
    CHECK(bravo.heard.at_ms == 3000);
    CHECK(bravo.heard.climb_age_ms == 1000);
}

// The difference rides on the velocity message, so the height derived from it is as old as it is.
TEST_CASE("adsb assembly: a GNSS height derives from a fresh difference and stops on a stale one") {
    Bravo bravo;
    REQUIRE(bravo.hear(adsb_ground_velocity(kAddress, kNoCapability, 0, 80, 0, 150), 0));
    bravo.fly(kLatitude, kLongitude, 1000, false, 3000);
    REQUIRE(bravo.heard.pressure_altitude_valid);
    CHECK(bravo.heard.pressure_altitude_ft == 3000);
    REQUIRE(bravo.heard.altitude_valid);
    CHECK(bravo.heard.altitude_m == to_metres(Feet(3150)).value);
    CHECK(bravo.heard.altitude_age_ms == 1000);

    bravo.fly(kLatitude, kLongitude, kHeightDifferenceMaxAgeMs, true, 3000);
    CHECK(bravo.heard.altitude_valid);
    bravo.fly(kLatitude, kLongitude, kHeightDifferenceMaxAgeMs + 1, false, 3000);
    CHECK(bravo.heard.pressure_altitude_valid);
    CHECK_FALSE(bravo.heard.altitude_valid);
}

TEST_CASE("adsb assembly: a GNSS height sent in the position wins over an older derived one") {
    Bravo bravo;
    REQUIRE(bravo.hear(adsb_ground_velocity(kAddress, kNoCapability, 0, 80, 0, 150), 0));
    bravo.fly(kLatitude, kLongitude, 500, false, 3000);
    REQUIRE(bravo.hear(
        adsb_airborne_position(kAddress, kNoCapability, kLatitude, kLongitude, true, 3500, true),
        1000));
    CHECK(bravo.heard.altitude_m == to_metres(Feet(3500)).value);
    CHECK(bravo.heard.altitude_age_ms == 0);
    CHECK(bravo.heard.pressure_altitude_ft == 3000);
    CHECK(bravo.heard.pressure_altitude_age_ms == 500);
}

TEST_CASE("adsb assembly: the Riddle's surface pair decodes near its airfield and not without it") {
    Bravo bravo;
    bravo.reference = reference_at(51.990, 4.375);
    REQUIRE(bravo.hear_raw("8c4841753aab238733c8cd4020b1", 0));
    REQUIRE(bravo.hear_raw("8c4841753a8a35323faebdac702d", 2000));
    REQUIRE(bravo.heard.position_valid);
    CHECK(bravo.heard.latitude_1e7 == e7(52.320607072215964));
    CHECK(bravo.heard.longitude_1e7 == e7(4.734734671456474));
    CHECK(flight::on_ground(bravo.heard.flight_state));

    REQUIRE(bravo.hear_raw("8c4841753a9a153237aef0f275be", 4000));
    CHECK(bravo.heard.latitude_1e7 == e7(52.32056051997815));
    CHECK(bravo.heard.longitude_1e7 == e7(4.735735212053571));

    Bravo lost;
    lost.reference = AdsbReference{};
    REQUIRE(lost.hear_raw("8c4841753aab238733c8cd4020b1", 0));
    REQUIRE(lost.hear_raw("8c4841753a8a35323faebdac702d", 2000));
    CHECK_FALSE(lost.heard.position_valid);
}

// A.1.7.8 gives a surface pair 50 s, or 25 s when either half may pass 25 kt or sends no speed.
TEST_CASE("adsb assembly: a surface pair waits 50 s when slow and 25 s when fast or unknown") {
    Bravo slow;
    REQUIRE(slow.hear(
        adsb_surface_position(kAddress, kNoCapability, kLatitude, kLongitude, false, 9), 0));
    REQUIRE(
        slow.hear(adsb_surface_position(kAddress, kNoCapability, kLatitude, kLongitude, true, 9),
                  kSlowSurfacePairMaxAgeMs));
    CHECK(slow.heard.position_valid);

    Bravo fast;
    REQUIRE(fast.hear(
        adsb_surface_position(kAddress, kNoCapability, kLatitude, kLongitude, false, 49), 0));
    REQUIRE(
        fast.hear(adsb_surface_position(kAddress, kNoCapability, kLatitude, kLongitude, true, 9),
                  kSurfacePairMaxAgeMs + 1));
    CHECK_FALSE(fast.heard.position_valid);

    Bravo silent;
    REQUIRE(silent.hear(
        adsb_surface_position(kAddress, kNoCapability, kLatitude, kLongitude, false, 0), 0));
    REQUIRE(
        silent.hear(adsb_surface_position(kAddress, kNoCapability, kLatitude, kLongitude, true, 0),
                    kSurfacePairMaxAgeMs));
    CHECK(silent.heard.position_valid);
}

TEST_CASE("adsb assembly: an airspeed and a heading never fill the ground speed and track") {
    Bravo bravo;
    REQUIRE(bravo.hear(adsb_air_velocity(kAddress, kNoCapability, 256, 120, -640), 0));
    CHECK_FALSE(bravo.heard.speed_valid);
    CHECK(bravo.heard.climb_valid);
    CHECK(bravo.heard.climb_source == model::ClimbSource::Barometric);

    REQUIRE(bravo.hear(adsb_ground_velocity(kAddress, kNoCapability, 100, 0, 640, 0), 1000));
    REQUIRE(bravo.heard.speed_valid);
    const uint16_t speed_q = bravo.heard.speed_q;
    CHECK(speed_q == to_speed_q(to_mm_s(Knots(100))).value);
    CHECK(bravo.heard.track_c9 == 128);
    CHECK(bravo.heard.climb_source == model::ClimbSource::Gnss);

    REQUIRE(bravo.hear(adsb_air_velocity(kAddress, kNoCapability, 0, 300, -1280), 3000));
    CHECK(bravo.heard.speed_q == speed_q);
    CHECK(bravo.heard.track_c9 == 128);
    CHECK(bravo.heard.speed_age_ms == 2000);
    CHECK(bravo.heard.climb_age_ms == 0);
    CHECK(bravo.heard.climb_e8 == to_climb_e8(to_mm_s(FeetPerMinute(-1280))).value);
}

TEST_CASE("adsb assembly: a surface position carries a ground speed and a track, and no climb") {
    Bravo bravo;
    AdsbFrame frame =
        adsb_surface_position(kAddress, kNoCapability, kLatitude, kLongitude, false, 49);
    adsb_set_message_bits(frame, 13, 13, 1);
    adsb_set_message_bits(frame, 14, 20, 32);
    REQUIRE(bravo.hear(frame, 0));
    CHECK(bravo.heard.speed_valid);
    CHECK(bravo.heard.speed_q == to_speed_q(to_mm_s(Knots(25))).value);
    CHECK(bravo.heard.track_c9 == 128);
    CHECK_FALSE(bravo.heard.climb_valid);
}

TEST_CASE("adsb assembly: the type code, else the capability, says where it is, and stays said") {
    Bravo bravo;
    REQUIRE(bravo.hear(adsb_identification(kAddress, kNoCapability, 3, 1, "DGLID"), 0));
    CHECK(flight::state_from(bravo.heard.flight_state) == flight::FlightState::Unknown);

    REQUIRE(bravo.hear(adsb_identification(kAddress, 4, 3, 1, "DGLID"), 1000));
    CHECK(flight::on_ground(bravo.heard.flight_state));
    REQUIRE(bravo.hear(adsb_identification(kAddress, 5, 3, 1, "DGLID"), 1500));
    CHECK(flight::airborne(bravo.heard.flight_state));
    REQUIRE(bravo.hear(adsb_identification(kAddress, 4, 3, 1, "DGLID"), 1800));
    REQUIRE(bravo.hear(adsb_ground_velocity(kAddress, kNoCapability, 0, 80, 0, 0), 2000));
    CHECK(flight::airborne(bravo.heard.flight_state));
    REQUIRE(bravo.hear(adsb_identification(kAddress, kNoCapability, 3, 1, "DGLID"), 5000));
    CHECK(flight::airborne(bravo.heard.flight_state));
    CHECK(bravo.heard.flight_state_age_ms == 3000);

    REQUIRE(bravo.hear(adsb_surface_position(kAddress, 5, kLatitude, kLongitude, false), 6000));
    CHECK(flight::on_ground(bravo.heard.flight_state));
    bravo.fly(kLatitude, kLongitude, 7000, true);
    CHECK(flight::airborne(bravo.heard.flight_state));
}

TEST_CASE("adsb assembly: the operational status names the version and, from version 2, the GVA") {
    Bravo bravo;
    REQUIRE(bravo.hear(adsb_operational_status(kAddress, kNoCapability, 0, 3), 0));
    CHECK(bravo.heard.adsb_version_valid);
    CHECK(bravo.heard.adsb_version == 0);
    CHECK_FALSE(bravo.heard.gva_valid);

    REQUIRE(bravo.hear(adsb_operational_status(kAddress, kNoCapability, 2, 2), 1000));
    CHECK(bravo.heard.adsb_version == 2);
    CHECK(bravo.heard.gva_valid);
    CHECK(bravo.heard.gva == 2);
}

TEST_CASE("adsb assembly: the aircraft's own address never becomes an observation") {
    Bravo bravo;
    bravo.assembly.set_own_address(kAddress);
    AdsbMessage message;
    REQUIRE(adsb_hear(adsb_airborne_position(kAddress, 5, kLatitude, kLongitude, false, 3000),
                      message) == Status::Ok);
    CHECK_FALSE(bravo.assembly.fold(message, 0, events::Stamp{}, bravo.reference, bravo.heard));
    CHECK(bravo.assembly.count() == 0);
    bravo.fly(kLatitude, kLongitude, 0, false, 3000, kAddress + 1);
    CHECK(bravo.assembly.count() == 1);
}

// A.1.7.10.2: a decode further than the receiver can hear is a bad pair, and both halves go.
TEST_CASE("adsb assembly: a pair that decodes past the reception ceiling is refused, and counted") {
    const double far_latitude = kHomeLatitude + 3.1;
    Bravo bravo;
    bravo.fly(far_latitude, kHomeLongitude, 0, false);
    bravo.fly(far_latitude, kHomeLongitude, 500, true);
    CHECK_FALSE(bravo.heard.position_valid);
    CHECK(bravo.assembly.implausible() == 1);

    Bravo lost;
    lost.reference = AdsbReference{};
    lost.fly(far_latitude, kHomeLongitude, 0, false);
    lost.fly(far_latitude, kHomeLongitude, 500, true);
    CHECK(lost.heard.position_valid);
}

// §2.2.10.6.3: inside 30 s a track moves 6 NM at most, so a single frame that says more is wrong.
TEST_CASE("adsb assembly: a tracked aircraft that jumps 7 NM inside 30 s keeps its position") {
    const double jumped_latitude = kLatitude + 7 * 1852 / 111320.0;
    Bravo bravo;
    bravo.fly(kLatitude, kLongitude, 0, false);
    bravo.fly(kLatitude, kLongitude, 500, true);
    REQUIRE(bravo.heard.position_valid);
    bravo.fly(jumped_latitude, kLongitude, 2000, false);
    CHECK(metres_from(bravo.heard, kLatitude, kLongitude) < 5.1);
    CHECK(bravo.heard.at_ms == 500);
    CHECK(bravo.assembly.implausible() == 1);
}

// §2.2.10.6.3 b validates a single frame only within 30 s of the last position, so past it a pair.
TEST_CASE("adsb assembly: after 30 s of silence a single frame moves nothing, and a pair does") {
    Bravo bravo;
    bravo.reference = reference_at(48.0, 8.0);
    for (uint32_t at_ms = 0; at_ms <= 1500; at_ms += 500)
        bravo.fly(48.0, 8.0, at_ms, (at_ms / 500) % 2 == 1);
    REQUIRE(bravo.heard.position_valid);

    bravo.fly(49.0, 8.0, 32500, false);
    CHECK(metres_from(bravo.heard, 48.0, 8.0) < 5.1);
    CHECK(bravo.heard.at_ms == 1500);

    bravo.fly(49.0, 8.0, 33000, true);
    CHECK(metres_from(bravo.heard, 49.0, 8.0) < 5.1);
    CHECK(bravo.heard.at_ms == 33000);
}

TEST_CASE("adsb assembly: a track continues on single frames, and on landing and takeoff") {
    Bravo bravo;
    bravo.fly(kLatitude, kLongitude, 0, false);
    bravo.fly(kLatitude, kLongitude, 500, true);
    const double moved_latitude = kLatitude + 0.01;
    bravo.fly(moved_latitude, kLongitude, 1500, true);
    CHECK(metres_from(bravo.heard, moved_latitude, kLongitude) < 5.1);

    REQUIRE(bravo.hear(
        adsb_surface_position(kAddress, kNoCapability, moved_latitude + 0.01, kLongitude, false),
        20000));
    CHECK(metres_from(bravo.heard, moved_latitude + 0.01, kLongitude) < 1.3);
    CHECK(flight::on_ground(bravo.heard.flight_state));
    bravo.fly(moved_latitude + 0.02, kLongitude, 40000, false);
    CHECK(metres_from(bravo.heard, moved_latitude + 0.02, kLongitude) < 5.1);
}

// §2.2.10.3.1 f: a track not refreshed for 120 s needs a new pair before it is believed again.
TEST_CASE("adsb assembly: a position not refreshed for 120 s is cleared, and a pair restarts it") {
    Bravo bravo;
    bravo.fly(kLatitude, kLongitude, 0, false);
    bravo.fly(kLatitude, kLongitude, 500, true);
    REQUIRE(bravo.heard.position_valid);

    bravo.fly(kLatitude, kLongitude, 500 + kTrackPositionMaxAgeMs + 1, false);
    CHECK_FALSE(bravo.heard.position_valid);
    bravo.fly(kLatitude, kLongitude, 500 + kTrackPositionMaxAgeMs + 500, true);
    CHECK(bravo.heard.position_valid);
}

// A.1.7.10.2: a second, independent pair must land where the track says, or the track was wrong.
TEST_CASE("adsb assembly: a track founded on a bad pair is dropped when an honest pair disagrees") {
    Bravo bravo;
    bravo.reference = AdsbReference{};
    bravo.fly(kLatitude, kLongitude, 0, false);
    bravo.fly(kLatitude + 0.2, kLongitude, 500, true);
    REQUIRE(bravo.heard.position_valid);
    REQUIRE(metres_from(bravo.heard, kLatitude, kLongitude) > 10000);

    bravo.fly(kLatitude, kLongitude, 1000, false);
    bravo.fly(kLatitude, kLongitude, 1500, true);
    CHECK_FALSE(bravo.heard.position_valid);
    CHECK(bravo.assembly.implausible() >= 1);
    bravo.fly(kLatitude, kLongitude, 2000, false);
    REQUIRE(bravo.heard.position_valid);
    CHECK(metres_from(bravo.heard, kLatitude, kLongitude) < 5.1);
}

TEST_CASE("adsb assembly: an honest track is confirmed by its next pair and stays") {
    Bravo bravo;
    for (uint32_t at_ms = 0; at_ms <= 5000; at_ms += 500)
        bravo.fly(kLatitude, kLongitude, at_ms, (at_ms / 500) % 2 == 1);
    CHECK(bravo.heard.position_valid);
    CHECK(bravo.heard.at_ms == 5000);
    CHECK(bravo.assembly.implausible() == 0);
}

// A rounding tie once put the two decodes of one frame 1e-7 degree apart, and dropped the track.
TEST_CASE("adsb assembly: an honest track on a rounding tie is confirmed and kept") {
    Bravo bravo;
    bravo.reference = reference_at(-6.0, -75.0);
    for (uint32_t at_ms = 0; at_ms <= 1500; at_ms += 500)
        bravo.fly(-5.98828125, -75.0, at_ms, (at_ms / 500) % 2 == 0);
    CHECK(bravo.heard.position_valid);
    CHECK(bravo.heard.at_ms == 1500);
    CHECK(bravo.assembly.implausible() == 0);
}

TEST_CASE("adsb assembly: an aircraft parked at 62.147250 N 46.582038 W keeps its track") {
    Bravo bravo;
    bravo.reference = reference_at(62.1, -46.5);
    for (uint32_t at_ms = 0; at_ms <= 20000; at_ms += 500) {
        REQUIRE(bravo.hear(adsb_surface_position(kAddress, kNoCapability, 62.147250, -46.582038,
                                                 (at_ms / 500) % 2 == 1),
                           at_ms));
        if (at_ms > 0) CHECK(bravo.heard.position_valid);
    }
    CHECK(bravo.assembly.implausible() == 0);
}

// fuzz_adsb_assembly found these: a single frame decoded against a polar track landed past 90 N.
TEST_CASE("adsb assembly: a frame that decodes past a pole is refused, and counted") {
    Bravo bravo;
    bravo.reference = AdsbReference{};
    REQUIRE(bravo.hear_raw("8a40006e6e6efeffffffff05f950", 1981470));
    REQUIRE(bravo.hear_raw("8e40006e6e6e88000000d0f82c7f", 1981470));
    REQUIRE(bravo.heard.position_valid);
    REQUIRE(bravo.heard.latitude_1e7 == 900000000);
    const uint32_t implausible = bravo.assembly.implausible();

    REQUIRE(bravo.hear_raw("8840006e6e1a00006e006eaa4853", 2009630));
    CHECK(bravo.heard.latitude_1e7 == 900000000);
    CHECK(bravo.assembly.implausible() == implausible + 1);
}

TEST_CASE("adsb assembly: a message heard a millisecond out of order keeps the aircraft") {
    Bravo bravo;
    bravo.fly(kLatitude, kLongitude, 1000, false);
    bravo.fly(kLatitude, kLongitude, 1500, true);
    REQUIRE(bravo.heard.position_valid);
    REQUIRE(bravo.hear(adsb_identification(kAddress, kNoCapability, 3, 1, "DGLID"), 1499));
    CHECK(bravo.heard.position_valid);
    CHECK(bravo.heard.at_ms == 1500);
    CHECK(bravo.heard.aircraft_category == 4);
}
