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
