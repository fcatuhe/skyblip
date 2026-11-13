// The observation frame Bravo writes and a Go reads, field by field, held to the registry's key
// table.
#include <cstdint>
#include <cstring>
#include <string>

#include "core/comms/peer.h"
#include "core/flight/state.h"
#include "core/settings/address.h"
#include "core/util/cbor.h"
#include "doctest/doctest.h"
#include "protocols/adsl/packet.h"
#include "test/support/config_frame.h"
#include "test/support/registry_frames.h"

using namespace skyblip;

namespace {

ConstByteSpan span_of(const std::string& frame) {
    return ConstByteSpan(reinterpret_cast<const uint8_t*>(frame.data()), frame.size());
}

Result<comms::PeerObservation> observation_of(const std::string& fields) {
    const std::string frame = complete_registry_frame(
        "{\"command\":\"observation\",\"address\":4479337,\"boot_epoch\":9,\"sequence\":4," +
        fields + "}");
    return comms::read_observation(span_of(frame));
}

}  // namespace

TEST_CASE("peer frames: an observation carries each valid field with its age, and nothing else") {
    model::AircraftObservation observation{};
    observation.address = 0x3C6586;
    observation.received = events::Stamp{1785628800, 250, true};
    observation.position_valid = true;
    observation.latitude_1e7 = 485000000;
    observation.longitude_1e7 = 85000000;
    observation.altitude_valid = false;
    observation.pressure_altitude_valid = true;
    observation.pressure_altitude_ft = 1800;
    observation.pressure_altitude_age_ms = 0;
    observation.speed_valid = true;
    observation.speed_q = 400;
    observation.track_c9 = 128;
    observation.speed_age_ms = 300;
    observation.flight_state = 2;
    observation.flight_state_age_ms = -40;
    comms::ObservationFrame header{};
    header.boot_epoch = 9;
    header.sequence = 3;
    header.callsign = "AFR123";
    header.callsign_heard_valid = true;
    header.callsign_heard_s = 1785628790;
    uint8_t frame[comms::kPeerPayloadFloorBytes];
    const int bytes = comms::write_observation(observation, header, frame, sizeof(frame));
    REQUIRE(bytes > 0);
    const std::string text =
        link_text(std::string(reinterpret_cast<const char*>(frame), static_cast<size_t>(bytes)));
    CHECK(text ==
          "{\"command\":\"observation\",\"address\":3958150,\"boot_epoch\":9,\"sequence\":3,"
          "\"at_s\":1785628800,\"into_ms\":250,\"latitude_1e7\":485000000,"
          "\"longitude_1e7\":85000000,\"pressure_altitude_ft\":1800,"
          "\"pressure_altitude_age_ms\":0,\"speed_q\":400,\"track_c9\":128,\"speed_age_ms\":300,"
          "\"flight_state\":\"airborne\",\"flight_state_age_ms\":-40,\"callsign\":\"AFR123\","
          "\"callsign_heard_s\":1785628790}");
    CHECK(complete_registry_frame(text) ==
          std::string(reinterpret_cast<const char*>(frame), static_cast<size_t>(bytes)));
}

// The Go reads Bravo's observation field by field, each with the age it was sent with (#193).
TEST_CASE("peer frames: an observation reads back every field, its validity and its age") {
    const Result<comms::PeerObservation> read = observation_of(
        "\"at_s\":1785628800,\"into_ms\":250,\"latitude_1e7\":485000000,"
        "\"longitude_1e7\":85000000,\"altitude_m\":1500,\"altitude_age_ms\":400,"
        "\"pressure_altitude_ft\":4500,\"pressure_altitude_age_ms\":-100,\"speed_q\":200,"
        "\"track_c9\":128,\"speed_age_ms\":300,\"climb_e8\":-16,\"climb_source\":\"barometric\","
        "\"climb_age_ms\":300,\"flight_state\":\"airborne\",\"flight_state_age_ms\":50,"
        "\"aircraft_category\":1,\"adsb_version\":2,\"gva\":2,\"callsign\":\"DLH4AB\","
        "\"callsign_heard_s\":1785628790");
    REQUIRE(read.ok());
    const comms::PeerObservation& peer = read.value();
    CHECK(peer.boot_epoch == 9);
    CHECK(peer.sequence == 4);
    CHECK(peer.at.at_s == 1785628800);
    CHECK(peer.at.into_ms == 250);
    CHECK(peer.at.phase_valid);
    const model::AircraftObservation& target = peer.observation;
    CHECK(target.address == 4479337);
    CHECK(target.address_mapping_table == adsl::kAddressTableIcao);
    CHECK(target.source == model::Source::Adsb);
    CHECK(target.position_valid);
    CHECK(target.latitude_1e7 == 485000000);
    CHECK(target.altitude_valid);
    CHECK(target.altitude_m == 1500);
    CHECK(target.altitude_age_ms == 400);
    CHECK(target.pressure_altitude_valid);
    CHECK(target.pressure_altitude_age_ms == -100);
    CHECK(target.speed_valid);
    CHECK(target.track_c9 == 128);
    CHECK(target.climb_valid);
    CHECK(target.climb_e8 == -16);
    CHECK(target.climb_source == model::ClimbSource::Barometric);
    CHECK(target.flight_state == static_cast<uint8_t>(flight::FlightState::Airborne));
    CHECK(target.flight_state_age_ms == 50);
    CHECK(target.adsb_version_valid);
    CHECK(target.gva_valid);
    CHECK(target.at_ms == 0);
    CHECK(std::string(peer.callsign) == "DLH4AB");
    CHECK(peer.callsign_dated);
    CHECK(peer.callsign_heard_s == 1785628790);
}

TEST_CASE("peer frames: a field Bravo has no reading of is absent, and read as invalid") {
    const Result<comms::PeerObservation> read = observation_of("\"at_s\":1785628800");
    REQUIRE(read.ok());
    const model::AircraftObservation& target = read.value().observation;
    CHECK_FALSE(read.value().at.phase_valid);
    CHECK_FALSE(target.position_valid);
    CHECK_FALSE(target.altitude_valid);
    CHECK_FALSE(target.pressure_altitude_valid);
    CHECK_FALSE(target.speed_valid);
    CHECK_FALSE(target.climb_valid);
    CHECK(target.flight_state == static_cast<uint8_t>(flight::FlightState::Unknown));
    CHECK_FALSE(target.adsb_version_valid);
    CHECK(read.value().callsign[0] == 0);
}

// The observation frame carries no RSSI, so a Go's $PFLAA must not print a level of 0 for it.
TEST_CASE("peer frames: an observation never carries a signal level") {
    const Result<comms::PeerObservation> read = observation_of("\"at_s\":1785628800");
    REQUIRE(read.ok());
    CHECK_FALSE(read.value().observation.rssi_valid);
}

TEST_CASE("peer frames: an observation with half a field, or a field without its age, is refused") {
    CHECK_FALSE(observation_of("\"at_s\":1,\"latitude_1e7\":485000000").ok());
    CHECK_FALSE(observation_of("\"at_s\":1,\"altitude_m\":1500").ok());
    CHECK_FALSE(observation_of("\"at_s\":1,\"climb_age_ms\":10").ok());
    CHECK_FALSE(observation_of("\"at_s\":1,\"speed_q\":200,\"speed_age_ms\":0").ok());
    CHECK_FALSE(observation_of("\"at_s\":1,\"callsign_heard_s\":1").ok());
    CHECK_FALSE(observation_of("\"at_s\":1,\"into_ms\":1000").ok());
    CHECK_FALSE(observation_of("\"at_s\":1,\"climb_e8\":8,\"climb_age_ms\":0").ok());
    CHECK_FALSE(observation_of("\"at_s\":1,\"climb_source\":\"gnss\",\"climb_age_ms\":0").ok());
    CHECK_FALSE(
        observation_of("\"at_s\":1,\"speed_q\":200,\"track_c9\":512,\"speed_age_ms\":0").ok());
    CHECK_FALSE(observation_of("\"at_s\":1,\"adsb_version\":8").ok());
    CHECK_FALSE(observation_of("\"at_s\":1,\"gva\":4").ok());
    CHECK_FALSE(observation_of("\"at_s\":1,\"climb_e8\":32768,\"climb_source\":\"gnss\","
                               "\"climb_age_ms\":0")
                    .ok());
    CHECK_FALSE(
        observation_of("\"at_s\":1,\"flight_state\":\"unknown\",\"flight_state_age_ms\":0").ok());
}

TEST_CASE("peer frames: an address past 24 bits, a code no enum names or a callsign not text") {
    const auto refused = [](uint32_t address, uint32_t climb_source, bool callsign_as_text) {
        uint8_t frame[96];
        cbor::Writer writer(frame, sizeof(frame));
        writer.add_code(link::kCommandKey, link::Command::Observation);
        writer.add_unsigned(link::observation_key::kAddress, address);
        writer.add_unsigned(link::observation_key::kBootEpoch, 9);
        writer.add_unsigned(link::observation_key::kSequence, 0);
        writer.add_unsigned(link::observation_key::kAtS, 1785628800);
        writer.add_integer(link::observation_key::kClimbE8, 8);
        writer.add_unsigned(link::observation_key::kClimbSource, climb_source);
        writer.add_integer(link::observation_key::kClimbAgeMs, 0);
        if (callsign_as_text)
            writer.add_text(link::observation_key::kCallsign, "DLH4AB");
        else
            writer.add_unsigned(link::observation_key::kCallsign, 4);
        const int bytes = writer.finish();
        REQUIRE(bytes > 0);
        return !comms::read_observation(ConstByteSpan(frame, static_cast<size_t>(bytes))).ok();
    };
    CHECK_FALSE(refused(0xFFFFFF, 2, true));
    CHECK(refused(0x1000000, 2, true));
    CHECK(refused(0xFFFFFF, 3, true));
    CHECK(refused(0xFFFFFF, 2, false));
}

// Every height and coordinate the Go computes with stays inside what ADS-B and the earth allow.
TEST_CASE("peer frames: a position off the earth or a height no ADS-B field encodes is refused") {
    CHECK(
        observation_of("\"at_s\":1,\"latitude_1e7\":900000000,\"longitude_1e7\":1800000000").ok());
    CHECK_FALSE(observation_of("\"at_s\":1,\"latitude_1e7\":900000001,\"longitude_1e7\":0").ok());
    CHECK_FALSE(observation_of("\"at_s\":1,\"latitude_1e7\":0,\"longitude_1e7\":-1800000001").ok());
    CHECK(observation_of("\"at_s\":1,\"altitude_m\":39572,\"altitude_age_ms\":0").ok());
    CHECK_FALSE(observation_of("\"at_s\":1,\"altitude_m\":39573,\"altitude_age_ms\":0").ok());
    CHECK(observation_of("\"at_s\":1,\"altitude_m\":-1318,\"altitude_age_ms\":0").ok());
    CHECK_FALSE(observation_of("\"at_s\":1,\"altitude_m\":-1319,\"altitude_age_ms\":0").ok());
    CHECK_FALSE(observation_of("\"at_s\":1,\"altitude_m\":2147483647,\"altitude_age_ms\":0").ok());
    CHECK(
        observation_of("\"at_s\":1,\"pressure_altitude_ft\":126700,\"pressure_altitude_age_ms\":0")
            .ok());
    CHECK_FALSE(
        observation_of("\"at_s\":1,\"pressure_altitude_ft\":126701,\"pressure_altitude_age_ms\":0")
            .ok());
    CHECK(observation_of("\"at_s\":1,\"pressure_altitude_ft\":-1200,\"pressure_altitude_age_ms\":0")
              .ok());
    CHECK_FALSE(
        observation_of("\"at_s\":1,\"pressure_altitude_ft\":-1201,\"pressure_altitude_age_ms\":0")
            .ok());
}
