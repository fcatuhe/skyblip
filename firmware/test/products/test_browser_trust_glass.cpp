// The press on a prompt also trusts a browser that proved a key, so the glass says so.
#include <string>

#include "core/comms/link_registry.h"
#include "core/util/hex.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/pages/confirm.h"
#include "test/support/glass_text.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

const crypto::Ed25519::Seed kBrowserSeed{0x42, 9};
const identity::Nonce kNonce{4, 3, 2, 1};

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

void say(Rig& rig, uint32_t& now_ms, const std::string& json) {
    rig.send(json.c_str());
    rig.run(now_ms, now_ms + 2000);
    now_ms += 2000;
}

bool reads_trust(Rig& rig) {
    return reads_in(rig.product.screen().framebuffer(), go::kConfirmTrustsBrowser, 0, 0,
                    go::kGlassWidth, go::kGlassHeight, go::kFrameScale);
}

}  // namespace

TEST_CASE("product: a prompt from a browser that proved a key says the press trusts it") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    rig.platform.random().queue(kNonce);
    say(rig, now_ms, "{\"command\":\"browser_challenge\"}");
    say(rig, now_ms, trust_browser());
    say(rig, now_ms, "{\"command\":\"set\",\"units\":\"metric\"}");
    REQUIRE(rig.product.config_link().config().pending() == comms::Pending::Set);
    CHECK(reads_trust(rig));
}

TEST_CASE("product: a prompt from a browser with no proven key says nothing of trust") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    say(rig, now_ms, "{\"command\":\"set\",\"units\":\"metric\"}");
    REQUIRE(rig.product.config_link().config().pending() == comms::Pending::Set);
    CHECK_FALSE(reads_trust(rig));
}
