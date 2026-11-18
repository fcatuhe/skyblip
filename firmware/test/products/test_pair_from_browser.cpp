// A browser pairs a whole Go with a whole Bravo by naming each one's tag to the other.
#include <string>
#include <vector>

#include "core/comms/link_registry.h"
#include "core/util/hex.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/pages/bravo.h"
#include "test/support/config_frame.h"
#include "test/support/paired_products.h"

using namespace skyblip;

namespace {

const crypto::Ed25519::Seed kBrowserSeed{0x42, 41};
const identity::Nonce kNonce{4, 4, 4, 4};
constexpr uint16_t kPhone = 2;
constexpr const char* kPairBravo = "{\"command\":\"pair\",\"tag\":16170}";
constexpr const char* kPairGo = "{\"command\":\"pair\",\"tag\":7259}";

std::string acknowledgement(bool accepted, const char* reason) {
    return std::string("{\"command\":\"acknowledgement\",\"acknowledge\":") +
           (accepted ? "true" : "false") + ",\"reason\":\"" + reason + "\"}";
}

std::string hex_text(ConstByteSpan bytes) {
    std::string out(2 * bytes.size(), '?');
    hex::write(bytes, out.data());
    return out;
}

std::string trust_browser() {
    std::string message = link::trust_browser_signed_over::kSignature;
    message.append(reinterpret_cast<const char*>(kNonce.data()), kNonce.size());
    const crypto::Ed25519::Signature signature = crypto::Ed25519::sign(
        kBrowserSeed,
        ConstByteSpan(reinterpret_cast<const uint8_t*>(message.data()), message.size()));
    const crypto::Ed25519::PublicKey key = crypto::Ed25519::public_key_of(kBrowserSeed);
    return "{\"command\":\"trust_browser\",\"key\":\"" +
           hex_text(ConstByteSpan(key.data(), key.size())) + "\",\"signature\":\"" +
           hex_text(ConstByteSpan(signature.data(), signature.size())) + "\"}";
}

struct Page {
    platform::host::Link& link;
    platform::host::Random& random;
    comms::ConfigService& config;
    uint16_t session;

    void connect() { link.raise_link(session, {1, {0x01, 0x02, 0x03, 0x04, 0x05, 0xC6}}); }

    void say(const std::string& json) {
        link.push_rx(link_frame(events::Endpoint::Config, session, json.c_str()));
    }

    void prove(bool trusted) {
        if (trusted)
            config.trusted_browsers().store(crypto::Ed25519::public_key_of(kBrowserSeed),
                                            "Chrome on Android");
        random.queue(kNonce);
        say("{\"command\":\"browser_challenge\"}");
    }

    std::vector<std::string> heard() const {
        std::vector<std::string> out;
        for (const platform::host::Link::Frame& frame : link.sent)
            if (frame.endpoint == events::Endpoint::Config && frame.session == session)
                out.push_back(link_text(frame.bytes));
        return out;
    }

    bool heard(const std::string& text) const {
        for (const std::string& each : heard())
            if (each == text) return true;
        return false;
    }
};

struct Pairing {
    PairedProducts pair;
    Page go{pair.go.platform.link(), pair.go.platform.random(),
            pair.go.product.config_link().config(), kPhone};
    Page bravo{pair.bravo.platform.link(), pair.bravo.platform.random(),
               pair.bravo.product.config_link().config(), kPhone};

    // INFO: fc 16nov26 a proof needs the challenge answered before the signature goes
    void prove(Page& page, bool trusted) {
        page.connect();
        pair.run_for(200);
        page.prove(trusted);
        pair.run_for(200);
        page.say(trust_browser());
        pair.run_for(200);
    }
};

}  // namespace

TEST_CASE("pair from a browser: each unit named, both pin, and the Go says paired unasked") {
    Pairing pairing;
    pairing.prove(pairing.bravo, /*trusted=*/true);
    pairing.bravo.say(kPairGo);
    pairing.pair.run_for(300);
    CHECK(pairing.bravo.heard(acknowledgement(true, "pair")));
    CHECK(pairing.pair.peer().accepting() == 0x1C5B);

    pairing.prove(pairing.go, /*trusted=*/true);
    pairing.go.say(kPairBravo);
    pairing.pair.run_for(300);
    CHECK(pairing.go.heard(acknowledgement(true, "pair")));
    CHECK(pairing.pair.go.product.screen().page() == go::Page::Bravo);
    pairing.pair.run_for(5000);

    CHECK(pairing.pair.go_pinned());
    CHECK(pairing.pair.go.settings().bravo == PairedLink::kPeripheralAddress);
    CHECK(pairing.pair.peer().paired());
    CHECK(bravo::address_of(pairing.pair.bravo.product.settings().peer) ==
          PairedLink::kCentralAddress);
    CHECK(pairing.go.heard().back() == acknowledgement(true, "paired"));
    CHECK_FALSE(pairing.pair.peer().accepting());

    pairing.go.say("{\"command\":\"get\"}");
    pairing.bravo.say("{\"command\":\"get\"}");
    pairing.pair.run_for(300);
    CHECK(pairing.go.heard().back().find("\"paired_tag\":16170") != std::string::npos);
    CHECK(pairing.bravo.heard().back().find("\"paired_tag\":7259") != std::string::npos);
    pairing.go.say(kPairBravo);
    pairing.pair.run_for(300);
    CHECK(pairing.go.heard().back() == acknowledgement(true, "paired"));
}

TEST_CASE("pair from a browser: the BRAVO page shows the search, then the wait for the button") {
    Pairing pairing;
    pairing.prove(pairing.go, /*trusted=*/true);
    pairing.pair.air.set_in_range(false);
    pairing.go.say(kPairBravo);
    pairing.pair.run_for(300);
    REQUIRE(pairing.pair.go.product.screen().page() == go::Page::Bravo);
    CHECK(pairing.pair.service().seeking() == 0x3F2A);
    CHECK(pairing.pair.service().step() == go::BravoStep::Scanning);

    pairing.pair.air.set_in_range(true);
    pairing.pair.run_for(2000);
    CHECK(pairing.pair.service().step() == go::BravoStep::Waiting);
    CHECK_FALSE(pairing.pair.service().seeking());
    CHECK(pairing.pair.bravo.product.peer().requested());

    pairing.pair.press_bravo();
    pairing.pair.run_for(3000);
    CHECK(pairing.pair.go_pinned());
    CHECK(pairing.go.heard().back() == acknowledgement(true, "paired"));
}

TEST_CASE("pair from a browser: the press that allows an untrusted browser's pair trusts it") {
    Pairing pairing;
    pairing.prove(pairing.go, /*trusted=*/false);
    CHECK(pairing.go.heard().back() == acknowledgement(false, "untrusted"));
    pairing.go.say(kPairBravo);
    pairing.pair.run_for(3000);
    comms::ConfigService& config = pairing.go.config;
    REQUIRE(config.pending() == comms::Pending::Pair);
    CHECK(config.prompt_trusts_browser());

    pairing.pair.go.press(pairing.pair.now_ms);
    pairing.pair.run_for(2000);
    CHECK(pairing.go.heard(acknowledgement(true, "pair")));
    CHECK(config.trusted_browsers().count() == 1);
    pairing.pair.press_bravo();
    pairing.pair.run_for(3000);
    CHECK(pairing.pair.go_pinned());
    CHECK(pairing.go.heard().back() == acknowledgement(true, "paired"));
}

TEST_CASE("pair from a browser: no Bravo with the tag in the connect ceiling is not_found") {
    Pairing pairing;
    pairing.prove(pairing.go, /*trusted=*/true);
    pairing.go.say("{\"command\":\"pair\",\"tag\":4660}");
    pairing.pair.run_for(go::BravoService::kConnectCeilingMs + 500);
    CHECK(pairing.go.heard().back() == acknowledgement(false, "not_found"));
    CHECK(pairing.pair.service().step() == go::BravoStep::Refused);
    CHECK(pairing.pair.service().refusal() == go::BravoRefusal::NotFound);
    CHECK_FALSE(pairing.pair.go_pinned());
}

TEST_CASE("pair from a browser: a Bravo nobody presses in the answer ceiling is no_answer") {
    Pairing pairing;
    pairing.prove(pairing.go, /*trusted=*/true);
    pairing.go.say(kPairBravo);
    pairing.pair.run_for(go::BravoService::kAnswerCeilingMs + 3000);
    CHECK(pairing.go.heard().back() == acknowledgement(false, "no_answer"));
}

TEST_CASE("pair from a browser: the pad leaves the search, and the browser hears cancelled") {
    Pairing pairing;
    pairing.prove(pairing.go, /*trusted=*/true);
    pairing.pair.air.set_in_range(false);
    pairing.go.say(kPairBravo);
    pairing.pair.run_for(1000);
    pairing.pair.go.tap(pairing.pair.now_ms);
    pairing.pair.run_for(300);
    CHECK(pairing.go.heard().back() == acknowledgement(false, "cancelled"));
    CHECK_FALSE(pairing.pair.service().seeking());
}

TEST_CASE("pair from a browser: a Go paired with another Bravo says farewell to it first") {
    Pairing pairing;
    pairing.pair.pair_and_store();
    REQUIRE(pairing.pair.go_pinned());
    REQUIRE(pairing.pair.peer().paired());

    pairing.prove(pairing.go, /*trusted=*/true);
    pairing.go.say("{\"command\":\"pair\",\"tag\":4660}");
    pairing.pair.run_for(go::BravoService::kFarewellCeilingMs +
                         go::BravoService::kConnectCeilingMs + 1000);
    CHECK_FALSE(pairing.pair.go_pinned());
    CHECK_FALSE(pairing.pair.peer().paired());
    CHECK(pairing.go.heard().back() == acknowledgement(false, "not_found"));
}

TEST_CASE("pair from a browser: Bravo answers only the Go it was named, and only for a minute") {
    Pairing pairing;
    pairing.prove(pairing.bravo, /*trusted=*/true);
    pairing.bravo.say("{\"command\":\"pair\",\"tag\":4660}");
    pairing.pair.run_for(300);
    pairing.pair.ask();
    pairing.pair.run_for(1000);
    CHECK(pairing.pair.peer().requested());
    CHECK_FALSE(pairing.pair.go_pinned());

    pairing.pair.run_for(bravo::PeerService::kAcceptanceWindowMs);
    CHECK_FALSE(pairing.pair.peer().accepting());
}

TEST_CASE("pair from a browser: an untrusted browser cannot arm Bravo's press") {
    Pairing pairing;
    pairing.prove(pairing.bravo, /*trusted=*/false);
    pairing.bravo.say(kPairGo);
    pairing.pair.run_for(300);
    CHECK(pairing.bravo.heard().back() == acknowledgement(false, "untrusted"));
    CHECK_FALSE(pairing.pair.peer().accepting());
}
