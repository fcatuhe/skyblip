// A whole Go pairs with the Bravo stub over a simulated link, keeps the pin, and keeps its slot.
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
#include "test/support/product_rig.h"
#include "test/support/tango.h"

using namespace skyblip;

namespace {

using Direction = PairedLink::Direction;
using Flow = PairedLink::Flow;

std::string settings_blob(Rig& rig) {
    uint8_t blob[64] = {};
    size_t bytes = 0;
    REQUIRE(rig.platform.key_value().read("settings", blob, sizeof(blob), bytes) == Status::Ok);
    return std::string(reinterpret_cast<const char*>(blob), bytes);
}

void store_settings(Rig& rig, const std::string& blob) {
    REQUIRE(rig.platform.key_value().write("settings",
                                           reinterpret_cast<const uint8_t*>(blob.data()),
                                           blob.size()) == Status::Ok);
}

}  // namespace

TEST_CASE("bravo pairing: an unpaired Go never scans, and shows no Bravo state") {
    Tango tango;
    tango.run_for(10000);
    CHECK(tango.go.platform.central().scans == 0);
    CHECK(tango.go.platform.central().connects == 0);
    CHECK_FALSE(go::paired(tango.go.settings()));
    CHECK(tango.go.platform.link().admits_phone());
}

TEST_CASE("bravo pairing: the BRAVO page scans, lists the tag, and pairs on two presses") {
    Tango tango;
    tango.open_page();
    CHECK(tango.go.product.screen().page() == go::Page::Bravo);
    CHECK(tango.service().step() == go::BravoStep::Scanning);
    REQUIRE(tango.service().found_count() == 1);
    CHECK(events::bluetooth_tag(tango.service().found(0)) == 0x3F2A);
    CHECK(tango.go.platform.central().scan_duty()->window_us ==
          go::BravoService::kPairingScanWindowUs);

    tango.go.press(tango.now_ms);
    tango.go.press(tango.now_ms);
    tango.run_for(2000);

    CHECK(tango.go.settings().bravo == PairedLink::kPeripheralAddress);
    CHECK(tango.service().link() == go::BravoLink::Up);
    CHECK(tango.service().step() == go::BravoStep::Idle);
    CHECK_FALSE(tango.go.platform.central().scanning());
    REQUIRE_FALSE(tango.bravo.claims.empty());
    CHECK(field(tango.bravo.claims[0], "command") == "peer_claim");
    CHECK(field(tango.bravo.claims[0], "alarm_requested") == "false");
}

TEST_CASE("bravo pairing: the pin survives a reboot, and the Go reconnects to it alone") {
    Tango tango;
    tango.pair_and_store();
    REQUIRE(go::paired(tango.go.settings()));
    const std::string blob = settings_blob(tango.go);

    Rig rebooted;
    store_settings(rebooted, blob);
    REQUIRE(rebooted.setup() == Status::Ok);
    CHECK(rebooted.settings().bravo == PairedLink::kPeripheralAddress);
    tango.go.world = nullptr;
    tango.air.reboot_central(rebooted.platform.central());
    rebooted.world = [&tango](uint32_t at_ms) {
        tango.bravo.run_pass(at_ms);
        tango.air.pump();
    };
    uint32_t now_ms = tango.now_ms;
    rebooted.run(now_ms, now_ms + 2000);
    now_ms += 2050;
    CHECK(rebooted.product.bravo().link() == go::BravoLink::Down);

    // Bravo holds its half until its supervision timeout, and then the Go comes back.
    tango.air.drop();
    rebooted.run(now_ms, now_ms + 3000);
    CHECK(rebooted.product.bravo().link() == go::BravoLink::Up);
    CHECK(rebooted.platform.central().scans == 0);
    CHECK(rebooted.platform.central().connect_duty().window_us ==
          go::BravoService::kReconnectScanWindowUs);
    for (uint16_t phone = 1; phone <= 3; phone++) rebooted.raise_link(phone);
    CHECK(rebooted.platform.link().links() == 2);
}

TEST_CASE("bravo pairing: a lost link reconnects to the pinned Bravo") {
    Tango tango;
    tango.pair();
    REQUIRE(tango.service().link() == go::BravoLink::Up);

    tango.air.set_in_range(false);
    tango.air.drop();
    tango.run_for(5000);
    CHECK(tango.service().link() == go::BravoLink::Down);

    tango.air.set_in_range(true);
    tango.run_for(2000);
    CHECK(tango.service().link() == go::BravoLink::Up);
    CHECK(tango.go.platform.central().connects >= 2);
}

TEST_CASE("bravo pairing: unpairing forgets the pin, drops the link and frees the phone slot") {
    Tango tango;
    tango.pair_and_store();
    tango.go.show(tango.now_ms, go::Page::Bravo);
    tango.go.press(tango.now_ms);
    REQUIRE(tango.go.product.screen().bravo_page().unpairing());
    tango.go.press(tango.now_ms);
    tango.run_for(5000);

    CHECK_FALSE(go::paired(tango.go.settings()));
    CHECK_FALSE(tango.go.platform.central().up());
    CHECK_FALSE(tango.bravo.link().up());
    go::Settings stored{};
    const std::string blob = settings_blob(tango.go);
    REQUIRE(go::from_blob(reinterpret_cast<const uint8_t*>(blob.data()), blob.size(), stored) ==
            Status::Ok);
    CHECK_FALSE(go::paired(stored));

    for (uint16_t phone = 1; phone <= 3; phone++) tango.go.raise_link(phone);
    CHECK(tango.go.platform.link().links() == 3);
}

TEST_CASE("bravo pairing: three phones connected refuse the pairing until one goes") {
    Tango tango;
    for (uint16_t phone = 1; phone <= 3; phone++) tango.go.raise_link(phone);
    tango.open_page();
    CHECK(tango.service().step() == go::BravoStep::Refused);
    CHECK(tango.service().refusal() == go::BravoRefusal::PhonesConnected);
    CHECK(tango.go.platform.central().scans == 0);

    tango.go.drop_link(2);
    tango.run_for(300);
    CHECK(tango.service().step() == go::BravoStep::Scanning);
    CHECK(tango.service().found_count() == 1);
    CHECK_FALSE(tango.go.platform.link().admits_phone());
}

TEST_CASE("bravo pairing: a paired Go refuses a third phone, Bravo connected or not") {
    Tango tango;
    tango.pair();
    tango.go.raise_link(1);
    tango.go.raise_link(2);
    tango.go.raise_link(3);
    CHECK(tango.go.platform.link().links() == 2);
    CHECK(tango.go.platform.link().refused() == 1);

    tango.air.set_in_range(false);
    tango.air.drop();
    tango.run_for(2000);
    tango.go.raise_link(3);
    CHECK(tango.go.platform.link().links() == 2);
    CHECK_FALSE(tango.go.platform.link().admits_phone());
}

TEST_CASE(
    "bravo pairing: a link below the widest peer frame refuses the pairing and writes nothing") {
    Tango tango;
    tango.bravo.link().declare_payload_bytes(comms::kPeerPayloadFloorBytes - 1);
    tango.pair();

    CHECK(tango.service().step() == go::BravoStep::Refused);
    CHECK(tango.service().refusal() == go::BravoRefusal::NarrowLink);
    CHECK_FALSE(go::paired(tango.go.settings()));
    CHECK(tango.bravo.claims.empty());
    CHECK(tango.air.delivered(Direction::ToPeripheral) == 0);
    CHECK(tango.go.platform.link().admits_phone());
}

TEST_CASE("bravo pairing: a Bravo whose button is never pressed refuses the pairing in time") {
    Tango tango;
    tango.air.set_flow(Direction::ToCentral, Flow::Lost);
    tango.pair();
    REQUIRE(tango.service().step() == go::BravoStep::Waiting);
    CHECK_FALSE(tango.bravo.claims.empty());

    tango.run_for(go::BravoService::kAnswerCeilingMs);
    CHECK(tango.service().step() == go::BravoStep::Refused);
    CHECK(tango.service().refusal() == go::BravoRefusal::NoAnswer);
    CHECK_FALSE(go::paired(tango.go.settings()));
    CHECK_FALSE(tango.go.platform.central().up());
}

TEST_CASE("bravo pairing: the pad cancels a pairing under way and goes back to the list") {
    Tango tango;
    tango.air.set_flow(Direction::ToCentral, Flow::Lost);
    tango.pair();
    REQUIRE(tango.service().step() == go::BravoStep::Waiting);

    tango.go.tap(tango.now_ms);
    tango.run_for(300);
    CHECK(tango.service().step() == go::BravoStep::Scanning);
    CHECK_FALSE(go::paired(tango.go.settings()));
    CHECK_FALSE(tango.go.platform.central().up());
}

TEST_CASE("bravo pairing: leaving the page stops the scan and frees the phone slot") {
    Tango tango;
    tango.open_page();
    REQUIRE(tango.go.platform.central().scanning());
    tango.go.long_touch(tango.now_ms);
    CHECK(tango.go.product.screen().page() == go::Page::Radar);
    CHECK_FALSE(tango.go.platform.central().scanning());
    CHECK(tango.go.platform.link().admits_phone());
    for (uint16_t phone = 1; phone <= 3; phone++) tango.go.raise_link(phone);
    CHECK(tango.go.platform.link().links() == 3);
}

// #191's contract: the notice once a boot, the self-test row while it stands.
TEST_CASE("bravo pairing: with no own ICAO on either side the Go says so once a boot") {
    Tango tango;
    tango.pair();
    CHECK(tango.service().own_icao_warning().stands(tango.go.settings().own_icao));
    CHECK(tango.go.product.screen().noticing());

    tango.go.tap(tango.now_ms);
    CHECK_FALSE(tango.go.product.screen().noticing());

    tango.air.set_in_range(false);
    tango.air.drop();
    tango.run_for(500);
    CHECK_FALSE(tango.service().own_icao_warning().stands(tango.go.settings().own_icao));
    tango.air.set_in_range(true);
    tango.run_for(4000);
    REQUIRE(tango.service().link() == go::BravoLink::Up);
    CHECK(tango.service().own_icao_warning().stands(tango.go.settings().own_icao));
    CHECK_FALSE(tango.go.product.screen().noticing());
}

TEST_CASE("bravo pairing: Bravo's own ICAO, or the Go's, keeps the warning down") {
    Tango tango;
    tango.bravo.health.own_icao_set = true;
    tango.pair();
    CHECK_FALSE(tango.service().own_icao_warning().stands(tango.go.settings().own_icao));
    CHECK_FALSE(tango.go.product.screen().noticing());

    tango.bravo.health.own_icao_set = false;
    tango.go.settings().own_icao = 0x3C6586;
    tango.run_for(2000);
    CHECK_FALSE(tango.service().own_icao_warning().stands(tango.go.settings().own_icao));
    CHECK_FALSE(tango.go.product.screen().noticing());
}

TEST_CASE("bravo pairing: the peer_claim carries the Go's own ICAO and its alarm volume") {
    Tango tango;
    tango.go.settings().own_icao = 0x3C6586;
    tango.go.settings().alarm_enabled = false;
    tango.pair();
    REQUIRE_FALSE(tango.bravo.claims.empty());
    const std::string& claim = tango.bravo.claims.back();
    CHECK(field(claim, "own_icao") == std::to_string(0x3C6586));
    CHECK(field(claim, "alarm_volume") == "off");
    CHECK(field(claim, "latitude_1e7").empty());
    CHECK(tango.go.platform.central().writes_refused == 0);
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
    CHECK_FALSE(tango.service().own_icao_warning().stands(tango.go.settings().own_icao));
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
    Tango tango;
    tango.air.set_in_range(false);
    tango.open_page();
    REQUIRE(tango.go.platform.central().scanning());
    for (uint8_t phone = 0; phone < 12; phone++)
        tango.go.platform.central().hear(events::BluetoothAddress{1, {phone, 0, 0, 0, 0, 0xC0}},
                                         "Pixel 8");
    tango.air.set_in_range(true);
    tango.air.pump();
    tango.run_for(300);
    REQUIRE(tango.service().found_count() == 1);
    CHECK(tango.service().found(0) == PairedLink::kPeripheralAddress);
}

TEST_CASE(
    "bravo pairing: a paired Go whose Bravo comes back too narrow holds off, unpinned never") {
    Tango tango;
    tango.pair();
    REQUIRE(tango.service().link() == go::BravoLink::Up);

    tango.air.drop();
    tango.bravo.link().declare_payload_bytes(comms::kPeerPayloadFloorBytes - 1);
    tango.run_for(4000);
    CHECK(tango.service().link() == go::BravoLink::Narrow);
    CHECK_FALSE(tango.go.platform.central().up());
    const int connects = tango.go.platform.central().connects;
    tango.run_for(10000);
    CHECK(tango.go.platform.central().connects == connects);
    CHECK(go::paired(tango.go.settings()));
}

// A loop that fails setup leaves a fresh image unconfirmed, so a missing link must not fail it.
TEST_CASE("bravo pairing: a paired Go on a board with no Bluetooth still comes up") {
    Tango tango;
    tango.pair_and_store();
    const std::string blob = settings_blob(tango.go);

    Rig bare(ports::without(platform::host::Platform::kFullyFitted, ports::Capability::Link));
    store_settings(bare, blob);
    CHECK(bare.setup() == Status::Ok);
    CHECK(go::paired(bare.settings()));
    bare.run(0, 2000);
    CHECK(bare.platform.central().connects == 0);
}
