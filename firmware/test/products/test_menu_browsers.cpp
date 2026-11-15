// The unit offered no gesture of its own to clear its trusted browsers, so a lost phone kept its
// power.
#include <string>

#include "core/comms/link_registry.h"
#include "core/util/hex.h"
#include "doctest/doctest.h"
#include "test/support/menu_rig.h"

namespace {

const crypto::Ed25519::Seed kBrowserSeed{0x42, 11};
const identity::Nonce kNonce{9, 8, 7, 6};

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

comms::ConfigService& config(Rig& rig) { return rig.product.config_link().config(); }

void say(Rig& rig, uint32_t& now_ms, const std::string& json) {
    rig.send(json.c_str());
    rig.run(now_ms, now_ms + 200);
    now_ms += 200;
}

void trust(Rig& rig, uint32_t& now_ms) {
    rig.platform.random().queue(kNonce);
    say(rig, now_ms, "{\"command\":\"browser_challenge\"}");
    say(rig, now_ms, trust_browser());
    say(rig, now_ms, "{\"command\":\"set\",\"units\":\"metric\"}");
    REQUIRE(config(rig).pending() == comms::Pending::Set);
    config(rig).confirm();
    rig.run(now_ms, now_ms + 10000);
    now_ms += 10000;
    REQUIRE(config(rig).trusted_browsers().count() == 1);
}

}  // namespace

TEST_CASE("menu: BROWSERS forgets every trusted browser on a second press") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    trust(rig, now_ms);
    const uint32_t writes = rig.platform.key_value().writes();

    open_menu(rig, now_ms);
    focus_on(rig, now_ms, go::MenuRow::Browsers);
    change(rig, now_ms);
    REQUIRE(rig.product.screen().editor().clearing());
    CHECK(config(rig).trusted_browsers().count() == 1);
    change(rig, now_ms);
    CHECK_FALSE(rig.product.screen().editor().clearing());
    CHECK(config(rig).trusted_browsers().count() == 0);
    rig.run(now_ms, now_ms + 10000);
    CHECK(rig.platform.key_value().writes() > writes);

    say(rig, now_ms, "{\"command\":\"set\",\"units\":\"nautical\"}");
    CHECK(config(rig).pending() == comms::Pending::Set);
}

TEST_CASE("menu: BROWSERS keeps the trusted browsers when the pad answers") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    trust(rig, now_ms);

    open_menu(rig, now_ms);
    focus_on(rig, now_ms, go::MenuRow::Browsers);
    change(rig, now_ms);
    REQUIRE(rig.product.screen().editor().clearing());
    move(rig, now_ms);
    CHECK_FALSE(rig.product.screen().editor().clearing());
    CHECK(config(rig).trusted_browsers().count() == 1);
}

TEST_CASE("menu: BROWSERS with none trusted asks nothing") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    open_menu(rig, now_ms);
    focus_on(rig, now_ms, go::MenuRow::Browsers);
    change(rig, now_ms);
    CHECK_FALSE(rig.product.screen().editor().clearing());
}
