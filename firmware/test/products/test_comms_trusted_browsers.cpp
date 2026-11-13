// A browser that signs the unit's nonce with a key two presses once approved needs no prompt.
#include <cstring>
#include <string>

#include "core/comms/config.h"
#include "core/comms/identity_link.h"
#include "core/identity/unit.h"
#include "core/util/hex.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/identity.h"
#include "hardware/platform/host/link.h"
#include "hardware/platform/host/platform.h"
#include "hardware/platform/host/watchdog.h"
#include "products/skyblip_go/settings_store.h"
#include "test/support/config_frame.h"

using namespace skyblip;
using platform::host::Identity;

namespace {

constexpr const char* kSet = "{\"command\":\"set\",\"units\":\"metric\"}";
constexpr const char* kDfu = "{\"command\":\"dfu\",\"version\":\"0.4.0+1\"}";
constexpr const char* kUntrusted =
    "{\"command\":\"acknowledgement\",\"acknowledge\":false,\"reason\":\"untrusted\"}";
constexpr const char* kTrusted =
    "{\"command\":\"acknowledgement\",\"acknowledge\":true,\"reason\":\"trusted\"}";

const crypto::Ed25519::Seed kBrowserSeed{0x42, 1, 2, 3};
const crypto::Ed25519::Seed kOtherBrowserSeed{0x43, 1, 2, 3};

std::string hex_text(ConstByteSpan bytes) {
    std::string out(2 * bytes.size(), '?');
    hex::write(bytes, out.data());
    return out;
}

std::string id_of(const crypto::Ed25519::Seed& seed) {
    const crypto::Ed25519::PublicKey key = crypto::Ed25519::public_key_of(seed);
    return hex_text(ConstByteSpan(key.data(), comms::BrowserId{}.size()));
}

struct Desk {
    platform::host::Link link;
    platform::host::Watchdog watchdog;
    platform::host::Dfu dfu{watchdog};
    go::Settings settings = go::defaults();
    go::SettingsStore store{settings, kTestActivation};
    comms::ConfigService config{link, store, &dfu};
    Identity port{std::nullopt};
    identity::Unit unit{port};
    comms::IdentityLink identity{unit};
    identity::Nonce nonce{5, 4, 3, 2, 1};

    Desk() {
        link.declare_payload_bytes(495);
        link.raise_link(1);
        dfu.running_version_valid = true;
        dfu.running_image_version = ports::ImageVersion{0, 3, 0, 1234};
        unit.load();
        config.set_identity(&identity);
        config.set_flight_state(flight::FlightState::Ground);
    }

    std::string say(const std::string& json) {
        const size_t before = link.sent.size();
        config.on_rx(config_frame(json.c_str()));
        return link.sent.size() == before ? std::string() : link_text(link.last().bytes);
    }

    std::string challenge() {
        port.queue_random(nonce);
        return say("{\"command\":\"browser_challenge\"}");
    }

    std::string prove(const crypto::Ed25519::Seed& seed, const identity::Nonce& signed_nonce,
                      const std::string& label = "Chrome on Android") {
        std::string message = "skyblip:browser:v1";
        message.append(reinterpret_cast<const char*>(signed_nonce.data()), signed_nonce.size());
        const crypto::Ed25519::Signature signature = crypto::Ed25519::sign(
            seed, ConstByteSpan(reinterpret_cast<const uint8_t*>(message.data()), message.size()));
        const crypto::Ed25519::PublicKey key = crypto::Ed25519::public_key_of(seed);
        return say("{\"command\":\"trust_browser\",\"key\":\"" +
                   hex_text(ConstByteSpan(key.data(), key.size())) + "\",\"signature\":\"" +
                   hex_text(ConstByteSpan(signature.data(), signature.size())) + "\",\"label\":\"" +
                   label + "\"}");
    }

    std::string connect_as(const crypto::Ed25519::Seed& seed) {
        challenge();
        return prove(seed, nonce);
    }

    void reconnect() {
        config.on_link_down(events::LinkDown{1});
        config.on_link_up(events::LinkUp{1, 495, kFirstConnection});
    }

    void enroll(const crypto::Ed25519::Seed& seed) {
        REQUIRE(connect_as(seed) == kUntrusted);
        say(kSet);
        REQUIRE(config.pending() == comms::Pending::Set);
        config.confirm();
        reconnect();
    }
};

}  // namespace

TEST_CASE("trusted browsers: a set two presses confirm trusts the browser that proved its key") {
    Desk desk;
    CHECK(desk.connect_as(kBrowserSeed) == kUntrusted);
    desk.say(kSet);
    REQUIRE(desk.config.pending() == comms::Pending::Set);
    desk.config.confirm();
    CHECK(desk.config.browsers_dirty());
    REQUIRE(desk.config.trusted_browsers().count() == 1);
    CHECK(std::string(desk.config.trusted_browsers().at(0).label) == "Chrome on Android");

    desk.reconnect();
    CHECK(desk.connect_as(kBrowserSeed) == kTrusted);
    CHECK(desk.say("{\"command\":\"set\",\"units\":\"nautical\"}") ==
          "{\"command\":\"acknowledgement\",\"acknowledge\":true}");
    CHECK(desk.config.pending() == comms::Pending::None);
    CHECK(desk.settings.units == go::Units::Nautical);
}

TEST_CASE(
    "trusted browsers: a dfu two presses confirm trusts its browser, and a trusted one opens "
    "the window at once") {
    Desk desk;
    REQUIRE(desk.connect_as(kBrowserSeed) == kUntrusted);
    desk.say(kDfu);
    REQUIRE(desk.config.pending() == comms::Pending::Dfu);
    desk.config.confirm();
    REQUIRE(desk.config.trusted_browsers().count() == 1);

    desk.reconnect();
    REQUIRE(desk.connect_as(kBrowserSeed) == kTrusted);
    CHECK(desk.say(kDfu) ==
          "{\"command\":\"acknowledgement\",\"acknowledge\":true,\"reason\":\"dfu\"}");
    CHECK(desk.config.pending() == comms::Pending::None);
    CHECK(desk.config.upload_allowed());
}

TEST_CASE("trusted browsers: a trusted browser sets in flight, and still starts no dfu there") {
    Desk desk;
    desk.enroll(kBrowserSeed);
    REQUIRE(desk.connect_as(kBrowserSeed) == kTrusted);
    desk.config.set_flight_state(flight::FlightState::Airborne);

    CHECK(desk.say(kSet) == "{\"command\":\"acknowledgement\",\"acknowledge\":true}");
    CHECK(desk.settings.units == go::Units::Metric);
    CHECK(desk.say(kDfu) ==
          "{\"command\":\"acknowledgement\",\"acknowledge\":false,\"reason\":\"in_flight\"}");
    CHECK_FALSE(desk.config.upload_allowed());
}

TEST_CASE("trusted browsers: trust ends with the link that proved it") {
    Desk desk;
    desk.enroll(kBrowserSeed);
    REQUIRE(desk.connect_as(kBrowserSeed) == kTrusted);
    desk.reconnect();

    desk.say(kSet);
    CHECK(desk.config.pending() == comms::Pending::Set);
}

TEST_CASE("trusted browsers: a proof needs a live nonce, spends it, and must verify") {
    Desk desk;
    const std::string no_session =
        "{\"command\":\"acknowledgement\",\"acknowledge\":false,\"reason\":\"no_session\"}";
    CHECK(desk.prove(kBrowserSeed, desk.nonce) == no_session);

    desk.challenge();
    const identity::Nonce other{9};
    CHECK(desk.prove(kBrowserSeed, other) ==
          "{\"command\":\"acknowledgement\",\"acknowledge\":false,\"reason\":\"bad_signature\"}");
    CHECK(desk.prove(kBrowserSeed, desk.nonce) == no_session);

    desk.challenge();
    desk.reconnect();
    CHECK(desk.prove(kBrowserSeed, desk.nonce) == no_session);
}

TEST_CASE("trusted browsers: a label wider than 24 bytes is refused invalid") {
    Desk desk;
    desk.challenge();
    CHECK(desk.prove(kBrowserSeed, desk.nonce, "Chrome on a very long phone name") ==
          "{\"command\":\"acknowledgement\",\"acknowledge\":false,\"reason\":\"invalid\"}");
}

TEST_CASE("trusted browsers: one press, an expiry and a confirmed recovery trust no browser") {
    Desk desk;
    REQUIRE(desk.connect_as(kBrowserSeed) == kUntrusted);
    desk.say(kSet);
    desk.config.cancel();
    desk.reconnect();

    REQUIRE(desk.connect_as(kBrowserSeed) == kUntrusted);
    desk.config.tick(0);
    desk.say(kSet);
    desk.config.tick(comms::kPromptCeilingMs);
    REQUIRE(desk.config.pending() == comms::Pending::None);

    desk.say("{\"command\":\"recovery\"}");
    REQUIRE(desk.config.pending() == comms::Pending::Recovery);
    desk.config.confirm();

    CHECK(desk.config.trusted_browsers().count() == 0);
    CHECK_FALSE(desk.config.browsers_dirty());
}

TEST_CASE("trusted browsers: only a trusted browser reads the list") {
    Desk desk;
    desk.enroll(kOtherBrowserSeed);
    desk.enroll(kBrowserSeed);
    CHECK(desk.connect_as(kBrowserSeed) == kTrusted);

    CHECK(desk.say("{\"command\":\"browsers\",\"index\":0}") ==
          "{\"command\":\"browser\",\"index\":0,\"count\":2,\"id\":\"" + id_of(kOtherBrowserSeed) +
              "\",\"label\":\"Chrome on Android\"}");
    CHECK(desk.say("{\"command\":\"browsers\",\"index\":1}").find(id_of(kBrowserSeed)) !=
          std::string::npos);
    CHECK(desk.say("{\"command\":\"browsers\",\"index\":2}") ==
          "{\"command\":\"browser\",\"index\":2,\"count\":2}");

    desk.reconnect();
    CHECK(desk.say("{\"command\":\"browsers\",\"index\":0}") == kUntrusted);
    CHECK(desk.say("{\"command\":\"forget_browser\",\"id\":\"" + id_of(kBrowserSeed) + "\"}") ==
          kUntrusted);
    CHECK(desk.config.trusted_browsers().count() == 2);
}

TEST_CASE(
    "trusted browsers: a trusted browser forgets another, then itself, and is trusted no "
    "more") {
    Desk desk;
    desk.enroll(kOtherBrowserSeed);
    desk.enroll(kBrowserSeed);
    REQUIRE(desk.connect_as(kBrowserSeed) == kTrusted);
    desk.config.clear_browsers_dirty();

    CHECK(
        desk.say("{\"command\":\"forget_browser\",\"id\":\"" + id_of(kOtherBrowserSeed) + "\"}") ==
        "{\"command\":\"acknowledgement\",\"acknowledge\":true,\"reason\":\"forgotten\"}");
    CHECK(desk.config.browsers_dirty());
    CHECK(
        desk.say("{\"command\":\"forget_browser\",\"id\":\"" + id_of(kOtherBrowserSeed) + "\"}") ==
        "{\"command\":\"acknowledgement\",\"acknowledge\":false,\"reason\":\"unknown_browser\"}");

    desk.say("{\"command\":\"forget_browser\",\"id\":\"" + id_of(kBrowserSeed) + "\"}");
    CHECK(desk.config.trusted_browsers().count() == 0);
    CHECK(desk.say("{\"command\":\"browsers\",\"index\":0}") == kUntrusted);
    desk.say(kSet);
    CHECK(desk.config.pending() == comms::Pending::Set);
}

TEST_CASE("trusted browsers: each frame is as wide as the registry says at its widest") {
    Desk desk;
    const std::string label(comms::kBrowserLabelBytes, 'x');
    desk.challenge();
    CHECK(desk.link.last().bytes.size() == comms::BrowserLink::kChallengeFrameBytes);
    std::string message = "skyblip:browser:v1";
    message.append(reinterpret_cast<const char*>(desk.nonce.data()), desk.nonce.size());
    const crypto::Ed25519::PublicKey key = crypto::Ed25519::public_key_of(kBrowserSeed);
    const crypto::Ed25519::Signature signature = crypto::Ed25519::sign(
        kBrowserSeed,
        ConstByteSpan(reinterpret_cast<const uint8_t*>(message.data()), message.size()));
    const std::string trust = "{\"command\":\"trust_browser\",\"key\":\"" +
                              hex_text(ConstByteSpan(key.data(), key.size())) +
                              "\",\"signature\":\"" +
                              hex_text(ConstByteSpan(signature.data(), signature.size())) +
                              "\",\"label\":\"" + label + "\"}";
    CHECK(config_frame(trust.c_str()).bytes == 133);
    CHECK(config_frame(
              ("{\"command\":\"forget_browser\",\"id\":\"" + id_of(kBrowserSeed) + "\"}").c_str())
              .bytes == 14);

    for (uint8_t number = 1; number < comms::TrustedBrowsers::kCapacity; number++) {
        crypto::Ed25519::PublicKey other{};
        other.fill(number);
        desk.config.trusted_browsers().store(other, label.c_str());
    }
    desk.config.trusted_browsers().store(key, label.c_str());
    REQUIRE(desk.connect_as(kBrowserSeed) == kTrusted);
    desk.say("{\"command\":\"browsers\",\"index\":7}");
    CHECK(desk.link.last().bytes.size() == comms::BrowserLink::kEntryFrameBytes);
}
