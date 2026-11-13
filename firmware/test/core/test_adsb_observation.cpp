// An ADS-B track as the observation the traffic table reads: ADS-L's units, table, category and
// flight state.
#include <cstdint>

#include "core/flight/state.h"
#include "core/model/aircraft.h"
#include "core/settings/address.h"
#include "core/units/units.h"
#include "doctest/doctest.h"
#include "test/support/adsb_frames.h"
#include "test/support/adsb_receiver.h"

using namespace skyblip;
using namespace skyblip::adsb_receiver;

TEST_CASE("adsb assembly: an observation is an ICAO ADS-B target with its own address") {
    Bravo bravo;
    bravo.fly(kLatitude, kLongitude, 0, false);
    CHECK(bravo.heard.address == kAddress);
    CHECK(bravo.heard.address_mapping_table == settings::kIcaoAddressMappingTable);
    CHECK(bravo.heard.source == model::Source::Adsb);
    CHECK(bravo.heard.received.at_s == kUtcS);
}

TEST_CASE("adsb assembly: a GNSS height reaches the observation in metres, with its age") {
    Bravo bravo;
    REQUIRE(bravo.hear(adsb_ground_velocity(kAddress, kNoCapability, 0, 80, 0, 150), 0));
    bravo.fly(kLatitude, kLongitude, 1000, false, 3000);
    REQUIRE(bravo.heard.altitude_valid);
    CHECK(bravo.heard.altitude_m == to_metres(Feet(3150)).value);
    CHECK(bravo.heard.altitude_age_ms == 1000);
    CHECK(bravo.heard.pressure_altitude_ft == 3000);
}

TEST_CASE("adsb assembly: a ground velocity reaches the observation as ADS-L speed and track") {
    Bravo bravo;
    REQUIRE(bravo.hear(adsb_air_velocity(kAddress, kNoCapability, 256, 120, -640), 0));
    CHECK_FALSE(bravo.heard.speed_valid);
    CHECK(bravo.heard.climb_source == model::ClimbSource::Barometric);

    REQUIRE(bravo.hear(adsb_ground_velocity(kAddress, kNoCapability, 100, 0, 640, 0), 1000));
    REQUIRE(bravo.heard.speed_valid);
    CHECK(bravo.heard.speed_q == to_speed_q(to_mm_s(Knots(100))).value);
    CHECK(bravo.heard.track_c9 == 128);
    CHECK(bravo.heard.climb_source == model::ClimbSource::Gnss);
    CHECK(bravo.heard.climb_e8 == to_climb_e8(to_mm_s(FeetPerMinute(640))).value);

    REQUIRE(bravo.hear(adsb_air_velocity(kAddress, kNoCapability, 0, 300, -1280), 3000));
    CHECK(bravo.heard.speed_q == to_speed_q(to_mm_s(Knots(100))).value);
    CHECK(bravo.heard.speed_age_ms == 2000);
    CHECK(bravo.heard.climb_e8 == to_climb_e8(to_mm_s(FeetPerMinute(-1280))).value);
}

TEST_CASE("adsb assembly: a surface movement reaches the observation as ADS-L speed and track") {
    Bravo bravo;
    AdsbFrame frame =
        adsb_surface_position(kAddress, kNoCapability, kLatitude, kLongitude, false, 49);
    adsb_set_message_bits(frame, 13, 13, 1);
    adsb_set_message_bits(frame, 14, 20, 32);
    REQUIRE(bravo.hear(frame, 0));
    CHECK(bravo.heard.speed_valid);
    CHECK(bravo.heard.speed_q == to_speed_q(to_mm_s(Knots(25))).value);
    CHECK(bravo.heard.track_c9 == 128);
    CHECK(flight::on_ground(bravo.heard.flight_state));

    Bravo stopped;
    REQUIRE(stopped.hear(
        adsb_surface_position(kAddress, kNoCapability, kLatitude, kLongitude, false, 1), 0));
    CHECK(stopped.heard.speed_valid);
    CHECK(stopped.heard.speed_q == 0);
    CHECK(stopped.heard.track_c9 == 0);
}

TEST_CASE("adsb assembly: the air-ground state reaches the observation as a G.1.2 code") {
    Bravo bravo;
    REQUIRE(bravo.hear(adsb_identification(kAddress, kNoCapability, 3, 1, "DGLID"), 0));
    CHECK(flight::state_from(bravo.heard.flight_state) == flight::FlightState::Unknown);
    CHECK(bravo.heard.aircraft_category == 4);
    REQUIRE(bravo.hear(adsb_identification(kAddress, 4, 3, 1, "DGLID"), 1000));
    CHECK(flight::on_ground(bravo.heard.flight_state));
    REQUIRE(bravo.hear(adsb_identification(kAddress, 5, 3, 1, "DGLID"), 1500));
    CHECK(flight::airborne(bravo.heard.flight_state));
    CHECK(bravo.heard.flight_state_age_ms == 0);
}
