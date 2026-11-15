// Bravo has no glass, so the Briefing device page is where its trusted browsers are read and
// cleared.
#include <string>

#include "core/comms/link_registry.h"
#include "core/util/hex.h"
#include "doctest/doctest.h"
#include "test/support/bravo_rig.h"

using namespace skyblip;

namespace {

const crypto::Ed25519::Seed kBrowserSeed{0x42, 13};
const identity::Nonce kNonce{5, 6, 7, 8};

std::string hex_text(ConstByteSpan bytes) {
    std::string out(2 * bytes.size(), '?');
    hex::write(bytes, out.data());
    return out;
}

std::string id_text() {
    const crypto::Ed25519::PublicKey key = crypto::Ed25519::public_key_of(kBrowserSeed);
    return hex_text(ConstByteSpan(key.data(), link::browser_bytes::kId));
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
           hex_text(ConstByteSpan(signature.data(), signature.size())) +
           "\",\"label\":\"Chrome on Android\"}";
}

std::string say(BravoRig& rig, const std::string& json) {
    rig.send(json.c_str());
    rig.run_for(200);
    return rig.last_on(events::Endpoint::Config);
}

}  // namespace

TEST_CASE(
    "bravo: a browser one press trusted lists itself, sets with no press, and forgets itself") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.platform.random().queue(kNonce);
    say(rig, "{\"command\":\"browser_challenge\"}");
    CHECK(say(rig, trust_browser()).find("untrusted") != std::string::npos);
    say(rig, "{\"command\":\"set\",\"alarm_volume\":\"soft\"}");
    comms::ConfigService& config = rig.product.config_link().config();
    REQUIRE(config.pending() == comms::Pending::Set);
    config.confirm();
    rig.run_for(200);
    REQUIRE(config.trusted_browsers().count() == 1);

    CHECK(say(rig, "{\"command\":\"browsers\",\"index\":0}") ==
          "{\"command\":\"browser\",\"index\":0,\"count\":1,\"id\":\"" + id_text() +
              "\",\"label\":\"Chrome on Android\"}");
    say(rig, "{\"command\":\"set\",\"alarm_volume\":\"loud\"}");
    CHECK(config.pending() == comms::Pending::None);

    CHECK(say(rig, "{\"command\":\"forget_browser\",\"id\":\"" + id_text() + "\"}")
              .find("forgotten") != std::string::npos);
    CHECK(config.trusted_browsers().count() == 0);
}
