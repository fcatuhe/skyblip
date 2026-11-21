// A Bravo installed from a .uf2 trusts the browser that wrote it, as a Go does.
#include <string>

#include "core/comms/link_registry.h"
#include "doctest/doctest.h"
#include "simulator/world/hex.h"
#include "test/support/bravo_rig.h"
#include "test/support/install_trust.h"

using namespace skyblip;

namespace {

const crypto::Ed25519::Seed kBrowserSeed{0x42, 21};
const identity::Nonce kNonce{2, 7, 1, 8};

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

std::string say(BravoRig& rig, const std::string& json) {
    rig.send(json.c_str());
    rig.run_for(200);
    return rig.last_on(events::Endpoint::Config);
}

}  // namespace

TEST_CASE(
    "bravo: the browser whose record the .uf2 carried sets with no press from the first boot") {
    BravoRig rig;
    rig.platform.dfu().running_slot = InstalledSlot::slot(
        InstalledSlot::record(crypto::Ed25519::public_key_of(kBrowserSeed), "Firefox"));
    REQUIRE(rig.setup() == Status::Ok);
    rig.run_for(10000);
    comms::ConfigService& config = rig.product.config_link().config();
    REQUIRE(config.trusted_browsers().count() == 1);
    CHECK(rig.platform.dfu().page_erases == 1);

    rig.platform.random().queue(kNonce);
    say(rig, "{\"command\":\"browser_challenge\"}");
    CHECK(say(rig, trust_browser()).find("\"trusted\"") != std::string::npos);
    say(rig, "{\"command\":\"set\",\"alarm\":\"soft\"}");
    CHECK(config.pending() == comms::Pending::None);
    CHECK(rig.product.settings().alarm == bravo::Alarm::Soft);
}
