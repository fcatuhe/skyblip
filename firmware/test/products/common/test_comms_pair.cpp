// pair at the config door: who may ask, what it answers, and where the Go's verdict goes.
#include <string>

#include "core/comms/identity_link.h"
#include "core/comms/link_registry.h"
#include "core/identity/unit.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/identity.h"
#include "hardware/platform/host/link.h"
#include "hardware/platform/host/platform.h"
#include "hardware/platform/host/random.h"
#include "hardware/platform/host/watchdog.h"
#include "products/common/services/config_service.h"
#include "products/skyblip_go/settings_store.h"
#include "simulator/world/hex.h"
#include "test/support/config_frame.h"

using namespace skyblip;

namespace {

const crypto::Ed25519::Seed kBrowserSeed{0x42, 31};
const identity::Nonce kNonce{8, 6, 7, 5};
constexpr uint16_t kBravoTag = 0x3F2A;

std::string acknowledgement(bool accepted, const char* reason) {
    return std::string("{\"command\":\"acknowledgement\",\"acknowledge\":") +
           (accepted ? "true" : "false") + ",\"reason\":\"" + reason + "\"}";
}

std::string hex_text(ConstByteSpan bytes) {
    std::string out(2 * bytes.size(), '?');
    hex::write(bytes, out.data());
    return out;
}

struct Desk {
    platform::host::Link link;
    platform::host::Watchdog watchdog;
    platform::host::Dfu dfu{watchdog};
    go::Settings settings = go::defaults();
    go::SettingsStore store{settings, kTestActivation, kNoOwnTransponder};
    common::ConfigService config{link, store, &dfu};
    platform::host::Identity port{std::nullopt};
    platform::host::Random random;
    identity::Unit unit{port, random};
    comms::IdentityLink identity{unit};

    explicit Desk(common::PairingSide side) {
        link.declare_payload_bytes(495);
        link.raise_link(1);
        unit.load();
        config.set_identity(&identity);
        config.serve_pairing(side);
        config.set_flight_state(flight::FlightState::Ground);
    }

    std::string say(const std::string& json) {
        const size_t before = link.sent.size();
        config.on_rx(config_frame(json.c_str()));
        return link.sent.size() == before ? std::string() : link_text(link.last().bytes);
    }

    std::string pair(uint32_t tag) {
        return say("{\"command\":\"pair\",\"tag\":" + std::to_string(tag) + "}");
    }

    void trust() {
        const crypto::Ed25519::PublicKey key = crypto::Ed25519::public_key_of(kBrowserSeed);
        config.trusted_browsers().store(key, "Chrome on Android");
        random.queue(kNonce);
        say("{\"command\":\"browser_challenge\"}");
        std::string message = link::trust_browser_signed_over::kSignature;
        message.append(reinterpret_cast<const char*>(kNonce.data()), kNonce.size());
        const crypto::Ed25519::Signature signature = crypto::Ed25519::sign(
            kBrowserSeed,
            ConstByteSpan(reinterpret_cast<const uint8_t*>(message.data()), message.size()));
        REQUIRE(say("{\"command\":\"trust_browser\",\"key\":\"" +
                    hex_text(ConstByteSpan(key.data(), key.size())) + "\",\"signature\":\"" +
                    hex_text(ConstByteSpan(signature.data(), signature.size())) + "\"}") ==
                acknowledgement(true, "trusted"));
    }
};

}  // namespace

TEST_CASE("pair: a trusted browser starts the Go's pairing at once, and hands it the tag once") {
    Desk desk(common::PairingSide::Go);
    desk.trust();
    CHECK(desk.pair(kBravoTag) == acknowledgement(true, "pair"));
    CHECK(desk.config.pending() == common::Pending::None);
    CHECK(desk.config.take_pair_request() == kBravoTag);
    CHECK_FALSE(desk.config.take_pair_request());
}

TEST_CASE("pair: an untrusted browser's pair asks on the Go's glass, and the press trusts it" *
          doctest::test_suite("main")) {
    Desk desk(common::PairingSide::Go);
    const crypto::Ed25519::PublicKey key = crypto::Ed25519::public_key_of(kBrowserSeed);
    desk.random.queue(kNonce);
    desk.say("{\"command\":\"browser_challenge\"}");
    std::string message = link::trust_browser_signed_over::kSignature;
    message.append(reinterpret_cast<const char*>(kNonce.data()), kNonce.size());
    const crypto::Ed25519::Signature signature = crypto::Ed25519::sign(
        kBrowserSeed,
        ConstByteSpan(reinterpret_cast<const uint8_t*>(message.data()), message.size()));
    REQUIRE(desk.say("{\"command\":\"trust_browser\",\"key\":\"" +
                     hex_text(ConstByteSpan(key.data(), key.size())) + "\",\"signature\":\"" +
                     hex_text(ConstByteSpan(signature.data(), signature.size())) + "\"}") ==
            acknowledgement(false, "untrusted"));

    CHECK(desk.pair(kBravoTag) ==
          "{\"command\":\"acknowledgement\",\"acknowledge\":false,\"pending\":true,"
          "\"reason\":\"confirm_pair\"}");
    CHECK(desk.config.pending() == common::Pending::Pair);
    CHECK(std::string(common::pending_title(common::Pending::Pair)) == "PAIR BRAVO");
    CHECK(std::string(desk.config.prompt_detail()) == "PAIR WITH BRAVO 3F2A");
    CHECK(desk.config.prompt_trusts_browser());
    CHECK_FALSE(desk.config.take_pair_request());

    desk.config.confirm();
    CHECK(link_text(desk.link.last().bytes) == acknowledgement(true, "pair"));
    CHECK(desk.config.take_pair_request() == kBravoTag);
    CHECK(desk.config.trusted_browsers().count() == 1);
    CHECK(desk.config.browsers_dirty());
}

TEST_CASE("pair: a press refused, a take-off or thirty seconds start no pairing") {
    Desk desk(common::PairingSide::Go);
    desk.pair(kBravoTag);
    desk.config.cancel();
    CHECK_FALSE(desk.config.take_pair_request());
    CHECK(desk.pair(kBravoTag) == acknowledgement(false, "refused_on_device"));

    Desk flying(common::PairingSide::Go);
    flying.pair(kBravoTag);
    flying.config.set_flight_state(flight::FlightState::Airborne);
    CHECK(link_text(flying.link.last_to(1)) == acknowledgement(false, "in_flight"));
    CHECK(flying.config.pending() == common::Pending::None);

    Desk waiting(common::PairingSide::Go);
    waiting.pair(kBravoTag);
    waiting.config.tick(common::kPromptCeilingMs);
    CHECK(link_text(waiting.link.last().bytes) == acknowledgement(false, "expired"));
    CHECK_FALSE(waiting.config.take_pair_request());
}

TEST_CASE("pair: refused in flight, invalid without a tag, out_of_range past 16 bits") {
    Desk desk(common::PairingSide::Go);
    desk.trust();
    CHECK(desk.say("{\"command\":\"pair\"}") == acknowledgement(false, "invalid"));
    CHECK(desk.pair(65536) == acknowledgement(false, "out_of_range"));
    desk.config.set_flight_state(flight::FlightState::Airborne);
    CHECK(desk.pair(kBravoTag) == acknowledgement(false, "in_flight"));
    CHECK_FALSE(desk.config.take_pair_request());
}

TEST_CASE("pair: a unit already paired with that tag answers paired, and its config names it") {
    Desk desk(common::PairingSide::Go);
    desk.config.set_paired_tag(kBravoTag);
    CHECK(desk.pair(kBravoTag) == acknowledgement(true, "paired"));
    CHECK(desk.config.pending() == common::Pending::None);
    CHECK(desk.say("{\"command\":\"get\"}").find("\"paired_tag\":16170}") != std::string::npos);

    desk.config.set_paired_tag(std::nullopt);
    CHECK(desk.say("{\"command\":\"get\"}").find("paired_tag") == std::string::npos);
    const size_t unpaired_bytes = desk.link.last().bytes.size();
    // Key 15 and a tag past 255 cost a byte and three, so the widest reply goes from 37 to 41.
    desk.config.set_paired_tag(0xFFFF);
    desk.say("{\"command\":\"get\"}");
    CHECK(desk.link.last().bytes.size() == unpaired_bytes + 4);
    CHECK(desk.say("{\"command\":\"defaults\"}").find("paired_tag") == std::string::npos);
}

TEST_CASE("pair: Bravo pairs only for a trusted browser, and never asks on a glass it lacks") {
    Desk desk(common::PairingSide::Bravo);
    CHECK(desk.pair(0x1C5B) == acknowledgement(false, "untrusted"));
    CHECK(desk.config.pending() == common::Pending::None);
    desk.trust();
    CHECK(desk.pair(0x1C5B) == acknowledgement(true, "pair"));
    CHECK(desk.config.take_pair_request() == 0x1C5B);
    desk.config.set_flight_state(flight::FlightState::Airborne);
    CHECK(desk.pair(0x1C5B) == acknowledgement(false, "in_flight"));
}

TEST_CASE("pair: a unit that pairs with nothing answers unknown_command") {
    Desk desk(common::PairingSide::None);
    CHECK(desk.pair(kBravoTag) == acknowledgement(false, "unknown_command"));
}

TEST_CASE("pair: the Go's verdict reaches the session that asked, and nobody once it dropped") {
    Desk desk(common::PairingSide::Go);
    desk.trust();
    desk.pair(kBravoTag);
    const size_t before = desk.link.sent.size();
    desk.config.pairing_ended(link::Reason::Paired);
    REQUIRE(desk.link.sent.size() == before + 1);
    CHECK(desk.link.last().session == 1);
    CHECK(link_text(desk.link.last().bytes) == acknowledgement(true, "paired"));
    desk.config.pairing_ended(link::Reason::NotFound);
    CHECK(desk.link.sent.size() == before + 1);

    desk.pair(kBravoTag);
    desk.config.on_link_down(events::LinkDown{1});
    desk.link.drop_link(1);
    const size_t dropped = desk.link.sent.size();
    desk.config.pairing_ended(link::Reason::NoAnswer);
    CHECK(desk.link.sent.size() == dropped);
    CHECK(desk.config.link_drops() == 0);
}

TEST_CASE("pair: a newer pair on the Go tells the browser of the one it replaced cancelled") {
    Desk desk(common::PairingSide::Go);
    desk.trust();
    desk.pair(kBravoTag);
    const size_t before = desk.link.sent.size();
    CHECK(desk.pair(0x01B7) == acknowledgement(true, "pair"));
    REQUIRE(desk.link.sent.size() == before + 2);
    CHECK(link_text(desk.link.sent[before].bytes) == acknowledgement(false, "cancelled"));
    CHECK(desk.config.take_pair_request() == 0x01B7);
}
