// The peer frames the Go writes and reads, held to the registry's key table.
#include <cstdint>
#include <string>

#include "core/comms/peer.h"
#include "core/util/cbor.h"
#include "doctest/doctest.h"
#include "test/support/config_frame.h"
#include "test/support/registry_frames.h"

using namespace skyblip;

namespace {

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
          "\"alarm_passes\":12,\"alarm_volume\":\"loud\",\"paired\":false}");
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
    claim.altitude_hae_valid = true;
    claim.altitude_hae_age_ms = 200;
    claim.pressure_altitude_valid = true;
    claim.pressure_altitude_mm = 1100000;
    claim.pressure_altitude_age_ms = 80;
    claim.own_icao = 0x3C6586;
    claim.alarm_passes = 3;
    claim.alarm_volume = 0;
    claim.paired = true;
    CHECK(claim_text(claim) ==
          "{\"command\":\"peer_claim\",\"boot_epoch\":7,\"at_s\":1785628800,\"into_ms\":250,"
          "\"latitude_1e7\":485000000,\"longitude_1e7\":-85000000,\"position_age_ms\":200,"
          "\"altitude_hae_mm\":1250000,\"altitude_hae_age_ms\":200,"
          "\"pressure_altitude_mm\":1100000,\"pressure_altitude_age_ms\":80,"
          "\"own_icao\":3958150,\"alarm_requested\":false,\"alarm_passes\":3,"
          "\"alarm_volume\":\"off\",\"paired\":true}");
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
        "\"export_reference\":\"own\",\"paired\":true}");
    const ConstByteSpan bytes(reinterpret_cast<const uint8_t*>(frame.data()), frame.size());
    REQUIRE(comms::peer_command(bytes).value() == link::Command::Health);
    const Result<comms::PeerHealth> health = comms::read_health(bytes);
    REQUIRE(health.ok());
    CHECK(health.value().boot_epoch == 9);
    CHECK(health.value().own_icao_set);
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
        "\"export_reference\":\"own\",\"paired\":true}");
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

namespace {

ConstByteSpan bytes_of(const std::string& frame) {
    return ConstByteSpan(reinterpret_cast<const uint8_t*>(frame.data()), frame.size());
}

std::string claim_frame(const std::string& fields) {
    return complete_registry_frame(
        "{\"command\":\"peer_claim\",\"boot_epoch\":7," + fields +
        "\"alarm_requested\":false,\"alarm_passes\":3,\"alarm_volume\":\"soft\",\"paired\":true}");
}

}  // namespace

TEST_CASE("peer frames: Bravo reads back every field of the claim the Go writes") {
    comms::PeerClaim written{};
    written.boot_epoch = 7;
    written.utc_valid = true;
    written.at_s = 1785628800;
    written.phase_valid = true;
    written.into_ms = 250;
    written.fix_valid = true;
    written.latitude_1e7 = 485000000;
    written.longitude_1e7 = -85000000;
    written.altitude_hae_mm = 1250000;
    written.fix_age_ms = 200;
    written.altitude_hae_valid = true;
    written.altitude_hae_age_ms = 5000;
    written.pressure_altitude_valid = true;
    written.pressure_altitude_mm = -1100000;
    written.pressure_altitude_age_ms = 80;
    written.own_icao = 0x3C6586;
    written.alarm_passes = 3;
    written.alarm_volume = 1;
    written.paired = true;
    uint8_t frame[comms::kPeerClaimWidestBytes];
    const int bytes = comms::write_peer_claim(written, frame, sizeof(frame));
    REQUIRE(bytes > 0);
    const Result<comms::PeerClaim> read =
        comms::read_peer_claim(ConstByteSpan(frame, static_cast<size_t>(bytes)));
    REQUIRE(read.ok());
    const comms::PeerClaim& claim = read.value();
    CHECK(claim.boot_epoch == 7);
    CHECK(claim.utc_valid);
    CHECK(claim.at_s == 1785628800);
    CHECK(claim.phase_valid);
    CHECK(claim.into_ms == 250);
    CHECK(claim.fix_valid);
    CHECK(claim.latitude_1e7 == 485000000);
    CHECK(claim.longitude_1e7 == -85000000);
    CHECK(claim.altitude_hae_mm == 1250000);
    CHECK(claim.fix_age_ms == 200);
    CHECK(claim.altitude_hae_valid);
    CHECK(claim.altitude_hae_age_ms == 5000);
    CHECK(claim.pressure_altitude_valid);
    CHECK(claim.pressure_altitude_mm == -1100000);
    CHECK(claim.pressure_altitude_age_ms == 80);
    CHECK(claim.own_icao == 0x3C6586u);
    CHECK(claim.alarm_passes == 3);
    CHECK(claim.alarm_volume == 1);
    CHECK(claim.paired);
}

// The claim crosses a trust boundary: half a field, or a value past its range, is refused whole.
TEST_CASE("peer frames: a claim with half a field or a value out of range is refused") {
    CHECK(comms::read_peer_claim(bytes_of(claim_frame("\"latitude_1e7\":485000000,"))).status() ==
          Status::Invalid);
    CHECK(comms::read_peer_claim(bytes_of(claim_frame("\"altitude_hae_mm\":500000,"))).status() ==
          Status::Invalid);
    CHECK(comms::read_peer_claim(bytes_of(claim_frame("\"pressure_altitude_mm\":500000,")))
              .status() == Status::Invalid);
    CHECK(comms::read_peer_claim(bytes_of(claim_frame("\"into_ms\":250,"))).status() ==
          Status::Invalid);
    CHECK(comms::read_peer_claim(bytes_of(claim_frame("\"at_s\":1785628800,\"into_ms\":1000,")))
              .status() == Status::OutOfRange);
    CHECK(comms::read_peer_claim(
              bytes_of(claim_frame("\"latitude_1e7\":900000001,\"longitude_1e7\":85000000,"
                                   "\"position_age_ms\":200,\"altitude_hae_mm\":0,"
                                   "\"altitude_hae_age_ms\":200,")))
              .status() == Status::OutOfRange);
    CHECK(comms::read_peer_claim(bytes_of(claim_frame("\"own_icao\":16777216,"))).status() ==
          Status::OutOfRange);
    CHECK(comms::read_peer_claim(bytes_of(complete_registry_frame(
                                     "{\"command\":\"health\",\"protocol_version\":1,"
                                     "\"boot_epoch\":9,\"listening\":true,\"utc_valid\":true,"
                                     "\"own_icao_set\":true,\"alarm_granted\":false,"
                                     "\"export_reference\":\"own\",\"paired\":true}")))
              .status() == Status::Invalid);

    uint8_t frame[64];
    cbor::Writer writer(frame, sizeof(frame));
    writer.add_code(link::kCommandKey, link::Command::PeerClaim);
    writer.add_unsigned(link::peer_claim_key::kBootEpoch, 7);
    writer.add_boolean(link::peer_claim_key::kAlarmRequested, false);
    writer.add_unsigned(link::peer_claim_key::kAlarmPasses, 3);
    writer.add_unsigned(link::peer_claim_key::kAlarmVolume, 2);
    const int bytes = writer.finish();
    REQUIRE(bytes > 0);
    CHECK(comms::read_peer_claim(ConstByteSpan(frame, static_cast<size_t>(bytes))).status() ==
          Status::Invalid);
}

TEST_CASE("peer frames: Bravo's health carries its state, and no last_sequence before the first") {
    comms::PeerHealth health{};
    health.boot_epoch = 9;
    health.listening = true;
    health.utc_valid = false;
    health.export_reference = comms::ExportReference::Go;
    uint8_t frame[comms::kPeerPayloadFloorBytes];
    int bytes = comms::write_health(health, frame, sizeof(frame));
    REQUIRE(bytes > 0);
    CHECK(
        link_text(std::string(reinterpret_cast<const char*>(frame), static_cast<size_t>(bytes))) ==
        "{\"command\":\"health\",\"protocol_version\":1,\"boot_epoch\":9,\"listening\":true,"
        "\"utc_valid\":false,\"own_icao_set\":false,\"alarm_granted\":false,"
        "\"export_reference\":\"go\",\"paired\":false}");

    health.last_sequence_valid = true;
    health.last_sequence = 41;
    health.own_icao_set = true;
    bytes = comms::write_health(health, frame, sizeof(frame));
    const Result<comms::PeerHealth> read =
        comms::read_health(ConstByteSpan(frame, static_cast<size_t>(bytes)));
    REQUIRE(read.ok());
    CHECK(read.value().boot_epoch == 9);
    CHECK(read.value().own_icao_set);
    CHECK_FALSE(read.value().paired);
}

// #192: a Go reads paired off every health to tell a pin Bravo holds from one it does not.
TEST_CASE("peer frames: a health frame without paired is refused") {
    uint8_t frame[64];
    cbor::Writer writer(frame, sizeof(frame));
    writer.add_code(link::kCommandKey, link::Command::Health);
    writer.add_unsigned(link::health_key::kProtocolVersion, 1);
    writer.add_unsigned(link::health_key::kBootEpoch, 9);
    writer.add_boolean(link::health_key::kListening, true);
    writer.add_boolean(link::health_key::kUtcValid, true);
    writer.add_boolean(link::health_key::kOwnIcaoSet, false);
    writer.add_boolean(link::health_key::kAlarmGranted, false);
    writer.add_unsigned(link::health_key::kExportReference, 2);
    const int bytes = writer.finish();
    REQUIRE(bytes > 0);
    CHECK(comms::read_health(ConstByteSpan(frame, static_cast<size_t>(bytes))).status() ==
          Status::Invalid);
}

// #192: each own-ship field is valid on its own, so a stale height leaves the position whole.
TEST_CASE("peer frames: a claim's position and height are read apart, each with its own age") {
    const Result<comms::PeerClaim> position_only = comms::read_peer_claim(
        bytes_of(claim_frame("\"latitude_1e7\":485000000,\"longitude_1e7\":85000000,"
                             "\"position_age_ms\":100,")));
    REQUIRE(position_only.ok());
    CHECK(position_only.value().fix_valid);
    CHECK(position_only.value().fix_age_ms == 100);
    CHECK_FALSE(position_only.value().altitude_hae_valid);

    const Result<comms::PeerClaim> both = comms::read_peer_claim(
        bytes_of(claim_frame("\"latitude_1e7\":485000000,\"longitude_1e7\":85000000,"
                             "\"position_age_ms\":100,\"altitude_hae_mm\":550000,"
                             "\"altitude_hae_age_ms\":5000,")));
    REQUIRE(both.ok());
    CHECK(both.value().fix_age_ms == 100);
    CHECK(both.value().altitude_hae_valid);
    CHECK(both.value().altitude_hae_age_ms == 5000);
}

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
