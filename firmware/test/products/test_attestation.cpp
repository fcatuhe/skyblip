// The unit signing the firmware it runs for skyblip.eu, with no prompt, as the attest frame asks.
#include <optional>
#include <string>

#include "core/comms/config.h"
#include "core/comms/identity_link.h"
#include "core/dfu/update.h"
#include "core/identity/unit.h"
#include "core/util/hex.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/identity.h"
#include "hardware/platform/host/link.h"
#include "hardware/platform/host/platform.h"
#include "hardware/platform/host/watchdog.h"
#include "products/skyblip_go/settings_store.h"
#include "test/support/activation_vectors.h"
#include "test/support/config_frame.h"

using namespace skyblip;
using platform::host::Identity;

namespace {

constexpr uint16_t kChromePayloadBytes = 495;

// INFO: fc 14nov26 the config endpoint of a unit running the vectors' 0.3.0+1234
struct Desk {
    platform::host::Link link;
    platform::host::Watchdog watchdog;
    platform::host::Dfu dfu{watchdog};
    go::Settings settings = go::defaults();
    go::SettingsStore store{settings, kTestActivation, kNoOwnTransponder};
    comms::ConfigService config{link, store, &dfu};
    Identity port;
    identity::Unit unit{port};
    comms::IdentityLink identity{unit};
    ActivationVectors vectors;

    explicit Desk(std::optional<uint16_t> serial = std::nullopt) : port(serial) {
        link.declare_payload_bytes(kChromePayloadBytes);
        link.raise_link(1);
        dfu.running_version_valid = true;
        dfu.running_image_version = ports::ImageVersion{0, 3, 0, 1234};
        dfu.release = true;
        unit.load();
        config.set_identity(&identity);
    }

    std::string say(const std::string& json) {
        config.on_rx(config_frame(json.c_str()));
        return link.sent.empty() ? std::string() : link_text(link.last().bytes);
    }

    std::string attest(const std::string& server_nonce) {
        return say("{\"command\":\"attest\",\"server_nonce\":\"" + server_nonce + "\"}");
    }

    std::string attest_with_vectors() { return attest(vectors.text("server_nonce")); }
};

std::string refusal(const char* word) {
    return std::string("{\"command\":\"acknowledgement\",\"acknowledge\":false,\"reason\":\"") +
           word + "\"}";
}

crypto::Ed25519::Signature signature_in(const std::string& frame) {
    crypto::Ed25519::Signature signature{};
    const size_t at = frame.find("\"signature\":\"") + 13;
    REQUIRE(hex::read(frame.substr(at, 128).c_str(), ByteSpan(signature.data(), signature.size())));
    return signature;
}

}  // namespace

// Ed25519 is deterministic, so the unit's signature is the one the server's suite checks.
TEST_CASE("attestation: the unit signs the vectors' attestation byte for byte, with no prompt") {
    Desk desk(Identity::kSerial);
    REQUIRE(desk.vectors.flag("release"));

    CHECK(desk.attest_with_vectors() == "{\"command\":\"attestation\",\"device_id\":\"" +
                                            desk.vectors.text("device_id") + "\",\"version\":\"" +
                                            desk.vectors.text("version") +
                                            "\",\"release\":true,\"signature\":\"" +
                                            desk.vectors.text("attestation_signature") + "\"}");
    CHECK(desk.config.pending() == comms::Pending::None);
}

TEST_CASE("attestation: a development build signs release false, and the signature covers it") {
    Desk desk(Identity::kSerial);
    desk.dfu.release = false;
    const std::string frame = desk.attest_with_vectors();
    REQUIRE(frame.find("\"release\":false,") != std::string::npos);

    const auto server_nonce = desk.vectors.bytes<16>("server_nonce");
    const crypto::Ed25519::PublicKey key = crypto::Ed25519::public_key_of(Identity::kUnitSeed);
    const crypto::Ed25519::Signature signature = signature_in(frame);
    CHECK(crypto::Ed25519::verify(
        signature, key,
        identity::attestation_message(Identity::kDeviceId, key, server_nonce, false, "0.3.0+1234")
            .bytes()));
    CHECK_FALSE(crypto::Ed25519::verify(
        signature, key,
        identity::attestation_message(Identity::kDeviceId, key, server_nonce, true, "0.3.0+1234")
            .bytes()));
}

// An activated unit transmits, so it keeps the flight gate every identity command keeps.
TEST_CASE("attestation: an activated unit refuses in flight, and one with no fix answers") {
    Desk flying(Identity::kSerial);
    flying.config.set_flight_state(flight::FlightState::Airborne);
    CHECK(flying.attest_with_vectors() == refusal("in_flight"));

    Desk indoors(Identity::kSerial);
    REQUIRE(indoors.config.flight_state() == flight::FlightState::Unknown);
    CHECK(indoors.attest_with_vectors().find("\"command\":\"attestation\"") != std::string::npos);
}

TEST_CASE("attestation: a unit with no key, or no version to report, signs nothing") {
    SUBCASE("no key") {
        Desk desk(Identity::kSerial);
        desk.port.key_provisioned = false;
        desk.unit.load();
        CHECK(desk.attest_with_vectors() == refusal("keyless"));
    }
    SUBCASE("no running version") {
        Desk desk(Identity::kSerial);
        desk.dfu.running_version_valid = false;
        CHECK(desk.attest_with_vectors() == refusal("keyless"));
    }
}

TEST_CASE("attestation: an attest without a server nonce is invalid") {
    Desk desk(Identity::kSerial);
    CHECK(desk.say("{\"command\":\"attest\"}") == refusal("invalid"));
}

// An iPhone's 182 bytes carry the frame, and a link one byte short of it is told before any
// signing.
TEST_CASE("attestation: a link that cannot carry the frame is answered small_mtu, not a drop") {
    Desk desk(Identity::kSerial);
    desk.link.declare_payload_bytes(comms::IdentityLink::kAttestationFrameBytes - 1);
    CHECK(desk.attest_with_vectors() == refusal("small_mtu"));
    CHECK(desk.config.link_drops() == 0);
}

// The widest version makes the widest frame, the size attest checks.
TEST_CASE("attestation: the frame at the widest version is exactly the size checked") {
    Desk desk(Identity::kSerial);
    desk.dfu.running_image_version = dfu::kWidestVersion;
    desk.dfu.release = false;
    desk.attest_with_vectors();
    CHECK(desk.link.last().bytes.size() == size_t(comms::IdentityLink::kAttestationFrameBytes));
    CHECK(link_text(desk.link.last().bytes).find("9.99.99+99999") != std::string::npos);
    CHECK(comms::IdentityLink::kAttestationFrameBytes <= comms::kSmallestSupportedPayloadBytes);
}

TEST_CASE(
    "attestation: a product that hands the config service no identity answers unknown_command") {
    platform::host::Link link;
    platform::host::Watchdog watchdog;
    platform::host::Dfu dfu{watchdog};
    go::Settings settings = go::defaults();
    go::SettingsStore store{settings, kTestActivation, kNoOwnTransponder};
    comms::ConfigService config{link, store, &dfu};
    link.declare_payload_bytes(kChromePayloadBytes);
    link.raise_link(1);
    config.on_rx(config_frame(
        "{\"command\":\"attest\",\"server_nonce\":\"00000000000000000000000000000000\"}"));
    CHECK(link_text(link.last().bytes) == refusal("unknown_command"));
}
