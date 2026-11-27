// A whole Go pairs with a whole Bravo over a simulated link, keeps the pin, and keeps its slot.
#include <cstdint>
#include <string>
#include <vector>

#include "core/comms/peer.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/pages/bravo.h"
#include "test/support/bravo_stub.h"
#include "test/support/config_frame.h"
#include "test/support/log_transfer.h"
#include "test/support/paired_link.h"
#include "test/support/paired_products.h"
#include "test/support/product_rig.h"
#include "test/support/tango.h"

using namespace skyblip;

namespace {

using Direction = PairedLink::Direction;

std::string settings_blob(Rig& rig) {
    uint8_t blob[64] = {};
    size_t bytes = 0;
    REQUIRE(rig.platform.key_value().read("settings", blob, sizeof(blob), bytes) == Status::Ok);
    return std::string(reinterpret_cast<const char*>(blob), bytes);
}

// Every peer_claim the Go wrote, named in JSON, whatever the air did with it.
std::vector<std::string> claims(const PairedProducts& pair) {
    std::vector<std::string> named;
    for (const std::string& frame : pair.go_wrote) {
        const std::string text = link_text(frame);
        if (field(text, "command") == "peer_claim") named.push_back(text);
    }
    return named;
}

void store_settings(Rig& rig, const std::string& blob) {
    REQUIRE(rig.platform.key_value().write("settings",
                                           reinterpret_cast<const uint8_t*>(blob.data()),
                                           blob.size()) == Status::Ok);
}

}  // namespace

TEST_CASE("bravo pairing: an unpaired Go never scans, and shows no Bravo state") {
    PairedProducts pair;
    pair.run_for(10000);
    CHECK(pair.go.platform.central().scans == 0);
    CHECK(pair.go.platform.central().connects == 0);
    CHECK_FALSE(go::paired(pair.go.settings()));
    CHECK(pair.go.platform.link().admits_phone());
}

TEST_CASE("bravo pairing: the BRAVO page scans, lists the tag, and pairs on two presses" *
          doctest::test_suite("main")) {
    PairedProducts pair;
    pair.open_page();
    CHECK(pair.go.product.screen().page() == go::Page::Bravo);
    CHECK(pair.service().step() == go::BravoStep::Scanning);
    REQUIRE(pair.service().found_count() == 1);
    CHECK(events::bluetooth_tag(pair.service().found(0)) == 0x3F2A);
    CHECK(pair.go.platform.central().scan_duty()->window_us ==
          go::BravoService::kPairingScanWindowUs);

    pair.go.press();
    pair.go.press();
    pair.run_for(2000);
    pair.press_bravo();
    pair.run_for(2000);

    CHECK(pair.go.settings().bravo == PairedLink::kPeripheralAddress);
    CHECK(pair.service().link() == go::BravoLink::Up);
    CHECK(pair.service().step() == go::BravoStep::Idle);
    CHECK_FALSE(pair.go.platform.central().scanning());
    REQUIRE_FALSE(claims(pair).empty());
    CHECK(field(claims(pair)[0], "command") == "peer_claim");
    CHECK(field(claims(pair)[0], "alarm") == "loud");
    CHECK(field(claims(pair)[0], "announcing") == "none");
}

TEST_CASE("bravo pairing: the pin survives a reboot, and the Go reconnects to it alone" *
          doctest::test_suite("main")) {
    PairedProducts pair;
    pair.pair_and_store();
    REQUIRE(go::paired(pair.go.settings()));
    const std::string blob = settings_blob(pair.go);

    Rig rebooted;
    store_settings(rebooted, blob);
    REQUIRE(rebooted.setup() == Status::Ok);
    CHECK(rebooted.settings().bravo == PairedLink::kPeripheralAddress);
    pair.go.world = nullptr;
    pair.air.reboot_central(rebooted.platform.central());
    rebooted.world = [&pair](uint32_t at_ms) {
        pair.bravo.now_ms = at_ms;
        pair.bravo.step();
        pair.air.pump();
    };
    rebooted.now_ms = pair.now_ms;
    rebooted.run(rebooted.now_ms, rebooted.now_ms + 2000);
    rebooted.now_ms += 2050;
    CHECK(rebooted.product.bravo().link() == go::BravoLink::Down);

    // Bravo holds its half until its supervision timeout, and then the Go comes back.
    pair.air.drop();
    rebooted.run(rebooted.now_ms, rebooted.now_ms + 3000);
    CHECK(rebooted.product.bravo().link() == go::BravoLink::Up);
    CHECK(rebooted.platform.central().scans == 0);
    CHECK(rebooted.platform.central().connect_duty().window_us ==
          go::BravoService::kReconnectScanWindowUs);
    for (uint16_t phone = 1; phone <= 3; phone++) rebooted.raise_link(phone);
    CHECK(rebooted.platform.link().links() == 2);
}

TEST_CASE("bravo pairing: a lost link reconnects to the pinned Bravo") {
    PairedProducts pair;
    pair.pair();
    REQUIRE(pair.service().link() == go::BravoLink::Up);

    pair.air.set_in_range(false);
    pair.air.drop();
    pair.run_for(5000);
    CHECK(pair.service().link() == go::BravoLink::Down);

    pair.air.set_in_range(true);
    pair.run_for(2000);
    CHECK(pair.service().link() == go::BravoLink::Up);
    CHECK(pair.go.platform.central().connects >= 2);
}

TEST_CASE("bravo pairing: unpairing forgets the pin, drops the link and frees the phone slot" *
          doctest::test_suite("main")) {
    PairedProducts pair;
    pair.pair_and_store();
    pair.go.show(go::Page::Bravo);
    pair.go.press();
    REQUIRE(pair.go.product.screen().bravo_page().unpairing());
    pair.go.press();
    pair.run_for(5000);
    pair.go.tap();
    pair.run_for(500);

    CHECK_FALSE(go::paired(pair.go.settings()));
    CHECK_FALSE(pair.go.platform.central().up());
    CHECK_FALSE(pair.bravo.platform.link().up());
    go::Settings stored{};
    const std::string blob = settings_blob(pair.go);
    REQUIRE(go::from_blob(reinterpret_cast<const uint8_t*>(blob.data()), blob.size(), stored) ==
            Status::Ok);
    CHECK_FALSE(go::paired(stored));

    for (uint16_t phone = 1; phone <= 3; phone++) pair.go.raise_link(phone);
    CHECK(pair.go.platform.link().links() == 3);
}

// Zephyr's central refuses a write while one is in flight, as a periodic claim may be.
TEST_CASE("bravo pairing: a farewell the central refuses once is written again, and unpins Bravo") {
    PairedProducts pair;
    pair.pair_and_store();
    REQUIRE(pair.peer().paired());
    pair.go.show(go::Page::Bravo);
    pair.go.press();
    REQUIRE(pair.go.product.screen().bravo_page().unpairing());
    pair.go.platform.central().writes_to_block = 1;
    pair.go.press();
    pair.run_for(5000);

    CHECK(pair.go.platform.central().writes_blocked == 1);
    CHECK_FALSE(pair.service().leaving());
    CHECK_FALSE(pair.peer().paired());
}

TEST_CASE("bravo pairing: a farewell the central always refuses is retried, and the Go leaves on time") {
    PairedProducts pair;
    pair.pair_and_store();
    pair.go.show(go::Page::Bravo);
    pair.go.press();
    REQUIRE(pair.go.product.screen().bravo_page().unpairing());
    pair.go.platform.central().writes_to_block = 1000;
    pair.go.press();

    pair.run_for(go::BravoService::kFarewellCeilingMs - 200);
    CHECK(pair.go.platform.central().writes_blocked > 1);
    CHECK(pair.service().leaving());
    CHECK(pair.go.platform.central().up());

    pair.run_for(400);
    CHECK_FALSE(pair.service().leaving());
    CHECK_FALSE(pair.go.platform.central().up());
}

// A pilot who unpairs a Bravo is often replacing it, so the page lists the Bravos in range.
TEST_CASE("bravo pairing: unpairing lists the Bravos in range, and the next press pairs one") {
    PairedProducts pair;
    pair.pair_and_store();
    pair.go.show(go::Page::Bravo);
    pair.go.press();
    pair.go.press();
    pair.run_for(5000);
    CHECK(pair.go.product.screen().page() == go::Page::Bravo);
    CHECK(pair.service().step() == go::BravoStep::Scanning);
    REQUIRE(pair.service().found_count() == 1);
    for (uint16_t phone = 1; phone <= 3; phone++) pair.go.raise_link(phone);
    CHECK(pair.go.platform.link().links() == 2);

    pair.go.press();
    pair.go.press();
    pair.run_for(2000);
    pair.press_bravo();
    pair.run_for(2000);
    CHECK(pair.go.settings().bravo == PairedLink::kPeripheralAddress);
    CHECK(pair.service().link() == go::BravoLink::Up);
}

TEST_CASE("bravo pairing: three phones connected refuse the pairing until one goes") {
    PairedProducts pair;
    for (uint16_t phone = 1; phone <= 3; phone++) pair.go.raise_link(phone);
    pair.open_page();
    CHECK(pair.service().step() == go::BravoStep::Refused);
    CHECK(pair.service().refusal() == go::BravoRefusal::PhonesConnected);
    CHECK(pair.go.platform.central().scans == 0);

    pair.go.drop_link(2);
    pair.run_for(300);
    CHECK(pair.service().step() == go::BravoStep::Scanning);
    CHECK(pair.service().found_count() == 1);
    CHECK_FALSE(pair.go.platform.link().admits_phone());
}

TEST_CASE("bravo pairing: a paired Go refuses a third phone, Bravo connected or not") {
    PairedProducts pair;
    pair.pair();
    pair.go.raise_link(1);
    pair.go.raise_link(2);
    pair.go.raise_link(3);
    CHECK(pair.go.platform.link().links() == 2);
    CHECK(pair.go.platform.link().refused() == 1);

    pair.air.set_in_range(false);
    pair.air.drop();
    pair.run_for(2000);
    pair.go.raise_link(3);
    CHECK(pair.go.platform.link().links() == 2);
    CHECK_FALSE(pair.go.platform.link().admits_phone());
}

TEST_CASE(
    "bravo pairing: a link below the widest peer frame refuses the pairing and writes nothing") {
    PairedProducts pair;
    pair.bravo.platform.link().declare_payload_bytes(comms::kPeerPayloadFloorBytes - 1);
    pair.ask();

    CHECK(pair.service().step() == go::BravoStep::Refused);
    CHECK(pair.service().refusal() == go::BravoRefusal::NarrowLink);
    CHECK_FALSE(go::paired(pair.go.settings()));
    CHECK(claims(pair).empty());
    CHECK(pair.air.delivered(Direction::ToPeripheral) == 0);
    CHECK(pair.go.platform.link().admits_phone());
}

TEST_CASE("bravo pairing: the pad cancels a pairing under way and goes back to the list") {
    PairedProducts pair;
    pair.ask();
    REQUIRE(pair.service().step() == go::BravoStep::Waiting);

    pair.go.tap();
    pair.run_for(300);
    CHECK(pair.service().step() == go::BravoStep::Scanning);
    CHECK_FALSE(go::paired(pair.go.settings()));
    CHECK_FALSE(pair.go.platform.central().up());
}

TEST_CASE("bravo pairing: leaving the page stops the scan and frees the phone slot") {
    PairedProducts pair;
    pair.open_page();
    REQUIRE(pair.go.platform.central().scanning());
    pair.go.long_touch();
    CHECK(pair.go.product.screen().page() == go::Page::Radar);
    CHECK_FALSE(pair.go.platform.central().scanning());
    CHECK(pair.go.platform.link().admits_phone());
    for (uint16_t phone = 1; phone <= 3; phone++) pair.go.raise_link(phone);
    CHECK(pair.go.platform.link().links() == 3);
}

// The self-test row stands while neither side has one. Only a transponder heard is a notice.
TEST_CASE("bravo pairing: no aircraft address on either side and nothing heard shows no notice") {
    PairedProducts pair;
    pair.pair();
    CHECK(pair.service().aircraft_address_warning().stands(pair.go.settings().aircraft_address));
    CHECK_FALSE(pair.go.product.screen().noticing());

    pair.air.set_in_range(false);
    pair.air.drop();
    pair.run_for(500);
    CHECK_FALSE(
        pair.service().aircraft_address_warning().stands(pair.go.settings().aircraft_address));
    pair.air.set_in_range(true);
    pair.run_for(4000);
    REQUIRE(pair.service().link() == go::BravoLink::Up);
    CHECK(pair.service().aircraft_address_warning().stands(pair.go.settings().aircraft_address));
    CHECK_FALSE(pair.go.product.screen().noticing());
}

TEST_CASE("bravo pairing: Bravo's aircraft address, or the Go's, keeps the warning down") {
    PairedProducts pair;
    pair.bravo.product.settings().aircraft_address = 0x3C6586;
    pair.pair();
    CHECK_FALSE(
        pair.service().aircraft_address_warning().stands(pair.go.settings().aircraft_address));
    CHECK_FALSE(pair.go.product.screen().noticing());

    pair.bravo.product.settings().aircraft_address = bravo::kAircraftAddressUnset;
    pair.go.settings().aircraft_address = 0x3C6586;
    pair.run_for(2000);
    CHECK_FALSE(
        pair.service().aircraft_address_warning().stands(pair.go.settings().aircraft_address));
    CHECK_FALSE(pair.go.product.screen().noticing());
}

TEST_CASE("bravo pairing: the peer_claim carries the Go's aircraft address") {
    PairedProducts pair;
    pair.go.settings().aircraft_address = 0x3C6586;
    pair.ask();
    REQUIRE_FALSE(claims(pair).empty());
    const std::string claim = claims(pair).back();
    CHECK(field(claim, "aircraft_address") == std::to_string(0x3C6586));
    CHECK(field(claim, "latitude_1e7").empty());
    CHECK(pair.go.platform.central().writes_refused == 0);
}

// Bravo's health says which peer frames it speaks, and a Go that reads another version pins
// nothing.
TEST_CASE("bravo pairing: a Bravo speaking an unknown peer protocol is refused and never pinned") {
    Tango tango;
    tango.bravo.health.protocol_version = 99;
    tango.pair();

    CHECK(tango.service().step() == go::BravoStep::Refused);
    CHECK(tango.service().refusal() == go::BravoRefusal::UnknownProtocol);
    CHECK_FALSE(go::paired(tango.go.settings()));
    CHECK_FALSE(tango.go.platform.central().up());
    CHECK_FALSE(
        tango.service().aircraft_address_warning().stands(tango.go.settings().aircraft_address));
    CHECK(tango.go.platform.link().admits_phone());
}

TEST_CASE("bravo pairing: a paired Bravo that comes back on an unknown protocol is held off") {
    Tango tango;
    tango.pair();
    REQUIRE(tango.service().link() == go::BravoLink::Up);

    tango.air.drop();
    tango.bravo.health.protocol_version = 99;
    tango.run_for(4000);
    CHECK(tango.service().link() == go::BravoLink::Unsupported);
    CHECK_FALSE(tango.go.platform.central().up());
    const int connects = tango.go.platform.central().connects;
    tango.run_for(10000);
    CHECK(tango.go.platform.central().connects == connects);
    CHECK(go::paired(tango.go.settings()));
}

// A scan hears every named advertiser in a crowded hangar, and Bravo is reported once a scan.
TEST_CASE("bravo pairing: a crowd of other advertisers never pushes Bravo off the list") {
    PairedProducts pair;
    pair.air.set_in_range(false);
    pair.open_page();
    REQUIRE(pair.go.platform.central().scanning());
    for (uint8_t phone = 0; phone < 12; phone++)
        pair.go.platform.central().hear(events::BluetoothAddress{1, {phone, 0, 0, 0, 0, 0xC0}},
                                        "Pixel 8");
    pair.air.set_in_range(true);
    pair.air.pump();
    pair.run_for(300);
    REQUIRE(pair.service().found_count() == 1);
    CHECK(pair.service().found(0) == PairedLink::kPeripheralAddress);
}

TEST_CASE(
    "bravo pairing: a paired Go whose Bravo comes back too narrow holds off, unpinned never") {
    PairedProducts pair;
    pair.pair();
    REQUIRE(pair.service().link() == go::BravoLink::Up);

    pair.air.drop();
    pair.bravo.platform.link().declare_payload_bytes(comms::kPeerPayloadFloorBytes - 1);
    pair.run_for(4000);
    CHECK(pair.service().link() == go::BravoLink::Narrow);
    CHECK_FALSE(pair.go.platform.central().up());
    const int connects = pair.go.platform.central().connects;
    pair.run_for(10000);
    CHECK(pair.go.platform.central().connects == connects);
    CHECK(go::paired(pair.go.settings()));
}

// A loop that fails setup leaves a fresh image unconfirmed, so a missing link must not fail it.
TEST_CASE("bravo pairing: a paired Go on a board with no Bluetooth still comes up") {
    PairedProducts pair;
    pair.pair_and_store();
    const std::string blob = settings_blob(pair.go);

    Rig bare(ports::without(platform::host::Platform::kFullyFitted, ports::Capability::Link));
    store_settings(bare, blob);
    CHECK(bare.setup() == Status::Ok);
    CHECK(go::paired(bare.settings()));
    bare.run(0, 2000);
    CHECK(bare.platform.central().connects == 0);
}
