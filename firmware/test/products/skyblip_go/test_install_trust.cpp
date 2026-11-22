// The browser that installed a Go from its .uf2 is trusted from the first boot, with no press.
#include <string>

#include "core/comms/link_registry.h"
#include "doctest/doctest.h"
#include "simulator/world/hex.h"
#include "test/support/install_trust.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

const crypto::Ed25519::Seed kBrowserSeed{0x42, 9};
const identity::Nonce kNonce{3, 1, 4, 1};

std::string hex_text(ConstByteSpan bytes) {
    std::string out(2 * bytes.size(), '?');
    hex::write(bytes, out.data());
    return out;
}

crypto::Ed25519::PublicKey browser_key() { return crypto::Ed25519::public_key_of(kBrowserSeed); }

std::string trust_browser() {
    std::string message = link::trust_browser_signed_over::kSignature;
    message.append(reinterpret_cast<const char*>(kNonce.data()), kNonce.size());
    const crypto::Ed25519::Signature signature = crypto::Ed25519::sign(
        kBrowserSeed,
        ConstByteSpan(reinterpret_cast<const uint8_t*>(message.data()), message.size()));
    const crypto::Ed25519::PublicKey key = browser_key();
    return "{\"command\":\"trust_browser\",\"key\":\"" +
           hex_text(ConstByteSpan(key.data(), key.size())) + "\",\"signature\":\"" +
           hex_text(ConstByteSpan(signature.data(), signature.size())) +
           "\",\"label\":\"Chrome on macOS\"}";
}

common::ConfigService& config(Rig& rig) { return rig.product.config_link().config(); }

void say(Rig& rig, uint32_t& now_ms, const std::string& json) {
    rig.send(json.c_str());
    rig.run(now_ms, now_ms + 200);
    now_ms += 200;
}

std::vector<uint8_t> installed_slot() {
    return InstalledSlot::slot(InstalledSlot::record(browser_key(), "Chrome on macOS"));
}

int stored_browsers(Rig& rig) {
    uint8_t blob[comms::TrustedBrowsers::kBlobBytes];
    size_t bytes = 0;
    if (rig.platform.key_value().read("browsers", blob, sizeof(blob), bytes) != Status::Ok)
        return -1;
    comms::TrustedBrowsers stored;
    REQUIRE(stored.adopt(blob, bytes));
    return stored.count();
}

}  // namespace

TEST_CASE("product: the browser whose record the .uf2 carried changes the settings with no press") {
    Rig rig;
    rig.platform.dfu().running_slot = installed_slot();
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    rig.run(now_ms, now_ms + 10000);
    now_ms += 10000;
    REQUIRE(config(rig).trusted_browsers().count() == 1);
    CHECK(std::string(config(rig).trusted_browsers().at(0).label) == "Chrome on macOS");
    CHECK(stored_browsers(rig) == 1);
    CHECK(rig.platform.dfu().page_erases == 1);
    CHECK(rig.platform.dfu().running_slot[InstalledSlot::kPage] == 0xFF);

    rig.platform.random().queue(kNonce);
    say(rig, now_ms, "{\"command\":\"browser_challenge\"}");
    say(rig, now_ms, trust_browser());
    CHECK(rig.last_on(events::Endpoint::Config) ==
          "{\"command\":\"acknowledgement\",\"acknowledge\":true,\"reason\":\"trusted\"}");
    say(rig, now_ms, "{\"command\":\"set\",\"units\":\"metric\"}");
    CHECK(config(rig).pending() == common::Pending::None);
    CHECK(rig.settings().units == go::Units::Metric);
}

// The page outlives the write until the erase lands, so a power cut there costs nothing.
TEST_CASE("product: a page the flash kept until the next boot adds the same browser once") {
    Rig before;
    before.platform.dfu().running_slot = installed_slot();
    before.platform.dfu().page_erase_fails = true;
    REQUIRE(before.setup() == Status::Ok);
    before.run(0, 10000);
    REQUIRE(stored_browsers(before) == 1);
    REQUIRE(before.platform.dfu().page_erases == 0);

    Rig after;
    after.platform.key_value() = before.platform.key_value();
    after.platform.dfu().running_slot = before.platform.dfu().running_slot;
    REQUIRE(after.setup() == Status::Ok);
    after.run(0, 10000);
    CHECK(config(after).trusted_browsers().count() == 1);
    CHECK(stored_browsers(after) == 1);
    CHECK(after.platform.dfu().page_erases == 1);
}

TEST_CASE("product: the trust page is erased only once the browsers blob holding its key landed") {
    Rig rig;
    rig.platform.dfu().running_slot = installed_slot();
    rig.platform.key_value().refuse_writes = true;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, 10000);
    CHECK(config(rig).trusted_browsers().count() == 1);
    CHECK(rig.platform.dfu().page_erases == 0);

    rig.platform.key_value().refuse_writes = false;
    rig.run(10000, 20000);
    CHECK(stored_browsers(rig) == 1);
    CHECK(rig.platform.dfu().page_erases == 1);
}

TEST_CASE("product: a trust record that does not verify is ignored, and its page is left alone") {
    std::vector<uint8_t> record = InstalledSlot::record(browser_key(), "Chrome on macOS");
    record[76] ^= 0x01;
    Rig rig;
    rig.platform.dfu().running_slot = InstalledSlot::slot(record);
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, 10000);
    CHECK(config(rig).trusted_browsers().count() == 0);
    CHECK(stored_browsers(rig) == -1);
    CHECK(rig.platform.dfu().page_erases == 0);
}
