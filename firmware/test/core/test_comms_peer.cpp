// The peer frames the Go writes and reads, held to the registry's key table.
#include <cstdint>
#include <string>

#include "core/comms/peer.h"
#include "core/flight/state.h"
#include "core/settings/address.h"
#include "core/util/cbor.h"
#include "doctest/doctest.h"
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

std::string claim_text(const comms::PeerClaim& claim) {
    uint8_t frame[comms::kPeerClaimWidestBytes];
    const int bytes = comms::write_peer_claim(claim, frame, sizeof(frame));
    REQUIRE(bytes > 0);
    return link_text(std::string(reinterpret_cast<const char*>(frame), static_cast<size_t>(bytes)));
}

}  // namespace

TEST_CASE("peer frames: a Go with no fix and no UTC claims only what it holds, and no alarm") {
    comms::PeerClaim claim{};
    claim.boot_epoch = 7;
    claim.alarm_passes = 12;
    claim.alarm_volume = 2;
    CHECK(claim_text(claim) ==
          "{\"command\":\"peer_claim\",\"boot_epoch\":7,\"alarm_requested\":false,"
          "\"alarm_passes\":12,\"alarm_volume\":\"loud\"}");
}

TEST_CASE("peer frames: a Go's peer_claim carries each own-ship field with its age") {
    comms::PeerClaim claim{};
    claim.boot_epoch = 7;
    claim.utc_valid = true;
    claim.at_s = 1785628800;
    claim.phase_valid = true;
    claim.into_ms = 250;
    claim.fix_valid = true;
    claim.latitude_1e7 = 485000000;
    claim.longitude_1e7 = -85000000;
    claim.altitude_hae_mm = 1250000;
    claim.fix_age_ms = 200;
    claim.pressure_altitude_valid = true;
    claim.pressure_altitude_mm = 1100000;
    claim.pressure_altitude_age_ms = 80;
    claim.own_icao = 0x3C6586;
    claim.alarm_passes = 3;
    claim.alarm_volume = 0;
    CHECK(claim_text(claim) ==
          "{\"command\":\"peer_claim\",\"boot_epoch\":7,\"at_s\":1785628800,\"into_ms\":250,"
          "\"latitude_1e7\":485000000,\"longitude_1e7\":-85000000,\"position_age_ms\":200,"
          "\"altitude_hae_mm\":1250000,\"altitude_hae_age_ms\":200,"
          "\"pressure_altitude_mm\":1100000,\"pressure_altitude_age_ms\":80,"
          "\"own_icao\":3958150,\"alarm_requested\":false,\"alarm_passes\":3,"
          "\"alarm_volume\":\"off\"}");
}

TEST_CASE("peer frames: a claim that will not fit its buffer is refused whole") {
    comms::PeerClaim claim{};
    claim.own_icao = 0x3C6586;
    uint8_t frame[8];
    CHECK(comms::write_peer_claim(claim, frame, sizeof(frame)) == 0);
}

TEST_CASE("peer frames: a health frame says whether Bravo holds an own ICAO") {
    const std::string frame = complete_registry_frame(
        "{\"command\":\"health\",\"protocol_version\":1,\"boot_epoch\":9,\"listening\":true,"
        "\"utc_valid\":true,\"own_icao_set\":true,\"alarm_granted\":false,"
        "\"export_reference\":\"own\"}");
    const ConstByteSpan bytes(reinterpret_cast<const uint8_t*>(frame.data()), frame.size());
    REQUIRE(comms::peer_command(bytes).value() == link::Command::Health);
    const Result<comms::PeerHealth> health = comms::read_health(bytes);
    REQUIRE(health.ok());
    CHECK(health.value().boot_epoch == 9);
    CHECK(health.value().own_icao_set);
    CHECK(health.value().listening);
    CHECK(health.value().utc_valid);
}

TEST_CASE("peer frames: an observation or a torn frame is no health") {
    const std::string observation = complete_registry_frame(
        "{\"command\":\"observation\",\"address\":4479337,\"boot_epoch\":9,\"sequence\":0,"
        "\"at_s\":1785628800}");
    CHECK_FALSE(
        comms::read_health(
            ConstByteSpan(reinterpret_cast<const uint8_t*>(observation.data()), observation.size()))
            .ok());
    const uint8_t torn[] = {0xA2, 0x00};
    CHECK_FALSE(comms::peer_command(ConstByteSpan(torn, sizeof(torn))).ok());
    CHECK_FALSE(comms::read_health(ConstByteSpan(torn, sizeof(torn))).ok());
}

// A Bravo speaking another version of the peer frames is never read as this one (#178).
TEST_CASE("peer frames: a health frame of an unknown or missing protocol version is refused") {
    const std::string newer = complete_registry_frame(
        "{\"command\":\"health\",\"protocol_version\":99,\"boot_epoch\":9,\"listening\":true,"
        "\"utc_valid\":true,\"own_icao_set\":true,\"alarm_granted\":false,"
        "\"export_reference\":\"own\"}");
    CHECK(comms::read_health(
              ConstByteSpan(reinterpret_cast<const uint8_t*>(newer.data()), newer.size()))
              .status() == Status::Unsupported);

    uint8_t frame[64];
    cbor::Writer writer(frame, sizeof(frame));
    writer.add_code(link::kCommandKey, link::Command::Health);
    writer.add_unsigned(link::health_key::kBootEpoch, 9);
    writer.add_boolean(link::health_key::kOwnIcaoSet, false);
    const int bytes = writer.finish();
    REQUIRE(bytes > 0);
    CHECK(comms::read_health(ConstByteSpan(frame, static_cast<size_t>(bytes))).status() ==
          Status::Invalid);
}

TEST_CASE("peer frames: a health frame that does not say whether Bravo listens is refused") {
    uint8_t deaf[64];
    cbor::Writer without_listening(deaf, sizeof(deaf));
    without_listening.add_code(link::kCommandKey, link::Command::Health);
    without_listening.add_unsigned(link::health_key::kProtocolVersion, 1);
    without_listening.add_unsigned(link::health_key::kBootEpoch, 9);
    without_listening.add_boolean(link::health_key::kUtcValid, true);
    without_listening.add_boolean(link::health_key::kOwnIcaoSet, false);
    const int deaf_bytes = without_listening.finish();
    REQUIRE(deaf_bytes > 0);
    CHECK(comms::read_health(ConstByteSpan(deaf, static_cast<size_t>(deaf_bytes))).status() ==
          Status::Invalid);
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
    CHECK(target.address_mapping_table == settings::kIcaoAddressMappingTable);
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

// protocol_version moves when a key changes meaning, not when Bravo adds one.
TEST_CASE("peer frames: a key a newer Bravo adds is skipped, not a reason to refuse") {
    uint8_t frame[64];
    cbor::Writer writer(frame, sizeof(frame));
    writer.add_code(link::kCommandKey, link::Command::Observation);
    writer.add_unsigned(link::observation_key::kAddress, 0x4459A9);
    writer.add_unsigned(link::observation_key::kBootEpoch, 9);
    writer.add_unsigned(link::observation_key::kSequence, 0);
    writer.add_unsigned(link::observation_key::kAtS, 1785628800);
    writer.add_unsigned(40, 7);
    const int bytes = writer.finish();
    REQUIRE(bytes > 0);
    const Result<comms::PeerObservation> read =
        comms::read_observation(ConstByteSpan(frame, static_cast<size_t>(bytes)));
    REQUIRE(read.ok());
    CHECK(read.value().observation.address == 0x4459A9);
}
