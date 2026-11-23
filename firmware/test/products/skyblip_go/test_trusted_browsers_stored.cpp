// The trusted browsers live in NVS beside the settings, written as the settings are.
#include <string>

#include "core/comms/link_registry.h"
#include "doctest/doctest.h"
#include "simulator/world/hex.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

const crypto::Ed25519::Seed kBrowserSeed{0x42, 7};
const identity::Nonce kNonce{1, 2, 3, 4};

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
           hex_text(ConstByteSpan(signature.data(), signature.size())) +
           "\",\"label\":\"Chrome on Android\"}";
}

common::ConfigService& config(Rig& rig) { return rig.product.config_link().config(); }

void say(Rig& rig, const std::string& json) {
    rig.send(json.c_str());
    rig.run_for(200);
}

void prove(Rig& rig) {
    rig.platform.random().queue(kNonce);
    say(rig, "{\"command\":\"browser_challenge\"}");
    say(rig, trust_browser());
}

}  // namespace

TEST_CASE("product: a browser a press trusted is still trusted after a restart") {
    Rig before;
    REQUIRE(before.setup() == Status::Ok);
    prove(before);
    say(before, "{\"command\":\"set\",\"units\":\"metric\"}");
    REQUIRE(config(before).pending() == common::Pending::Set);
    config(before).confirm();
    before.run_for(10000);

    Rig after;
    after.platform.key_value() = before.platform.key_value();
    REQUIRE(after.setup() == Status::Ok);
    REQUIRE(config(after).trusted_browsers().count() == 1);
    prove(after);
    CHECK(after.last_on(events::Endpoint::Config) ==
          "{\"command\":\"acknowledgement\",\"acknowledge\":true,\"reason\":\"trusted\"}");
    say(after, "{\"command\":\"set\",\"units\":\"nautical\"}");
    CHECK(config(after).pending() == common::Pending::None);
    CHECK(after.settings().units == go::Units::Nautical);
}

TEST_CASE("product: a forget the flash refused is written again once the flash takes it") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    prove(rig);
    say(rig, "{\"command\":\"set\",\"units\":\"metric\"}");
    config(rig).confirm();
    rig.run_for(10000);
    const uint32_t writes = rig.platform.key_value().writes();

    rig.platform.key_value().refuse_writes = true;
    prove(rig);
    const crypto::Ed25519::PublicKey key = crypto::Ed25519::public_key_of(kBrowserSeed);
    say(rig, "{\"command\":\"forget_browser\",\"id\":\"" + hex_text(ConstByteSpan(key.data(), 8)) +
                 "\"}");
    rig.run_for(10000);
    CHECK(rig.platform.key_value().writes() == writes);

    rig.platform.key_value().refuse_writes = false;
    rig.run(rig.now_ms, rig.now_ms + 10000);
    uint8_t blob[comms::TrustedBrowsers::kBlobBytes];
    size_t bytes = 0;
    REQUIRE(rig.platform.key_value().read("browsers", blob, sizeof(blob), bytes) == Status::Ok);
    comms::TrustedBrowsers stored;
    REQUIRE(stored.adopt(blob, bytes));
    CHECK(stored.count() == 0);
}
