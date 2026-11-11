// A whole Go pairs with a whole Bravo over the simulated link, and neither keeps half a pin (#192).
#include <cstdint>
#include <optional>
#include <string>

#include "doctest/doctest.h"
#include "test/support/bravo_rig.h"
#include "test/support/log_transfer.h"
#include "test/support/paired_link.h"
#include "test/support/product_rig.h"
#include "test/support/registry_frames.h"

using namespace skyblip;

namespace {

using Direction = PairedLink::Direction;
using Flow = PairedLink::Flow;

constexpr events::BluetoothAddress kSecondGo{1, {0x11, 0x22, 0x33, 0x44, 0x55, 0xE8}};

std::string stored_blob(platform::host::MeshtrackerX1& platform) {
    uint8_t blob[64] = {};
    size_t bytes = 0;
    if (platform.key_value().read("settings", blob, sizeof(blob), bytes) != Status::Ok) return {};
    return std::string(reinterpret_cast<const char*>(blob), bytes);
}

bool stored_pin(platform::host::MeshtrackerX1& platform, events::BluetoothAddress& out) {
    const std::string blob = stored_blob(platform);
    bravo::Settings stored{};
    if (bravo::from_blob(reinterpret_cast<const uint8_t*>(blob.data()), blob.size(), stored) !=
        Status::Ok)
        return false;
    out = bravo::address_of(stored.peer);
    return stored.peer.pinned;
}

struct Tango {
    Rig go{};
    BravoRig bravo{};
    PairedLink air{bravo.platform.link(), go.platform.central()};
    uint32_t now_ms{100};

    Tango() {
        REQUIRE(go.setup() == Status::Ok);
        REQUIRE(bravo.setup() == Status::Ok);
        go.world = [this](uint32_t at_ms) {
            bravo.now_ms = at_ms;
            bravo.step();
            air.pump();
        };
    }

    go::BravoService& service() { return go.product.bravo(); }
    bravo::PeerService& peer() { return bravo.product.peer(); }

    void run_for(uint32_t span_ms) {
        go.run(now_ms, now_ms + span_ms);
        now_ms += span_ms + 50;
    }

    // The pilot opens the BRAVO page on the Go, picks the one listed and confirms it.
    void ask() {
        go.show(now_ms, go::Page::Bravo);
        run_for(300);
        REQUIRE(service().found_count() == 1);
        go.press(now_ms);
        go.press(now_ms);
        run_for(2000);
    }

    void press_bravo() {
        bravo.platform.set_button_down(true);
        run_for(150);
        bravo.platform.set_button_down(false);
        run_for(150);
    }

    // INFO: fc 11nov26 each side's settings write waits for a write window, forced after 3 s
    void pair_and_store() {
        ask();
        press_bravo();
        run_for(5000);
    }

    bool go_pinned() { return go::paired(go.settings()); }
};

}  // namespace

TEST_CASE("bravo pairing: a Go's request blinks the lamp, and one press pairs both sides") {
    Tango tango;
    tango.ask();
    CHECK(tango.service().step() == go::BravoStep::Waiting);
    CHECK(tango.peer().requested());
    CHECK(tango.bravo.lamp() == indication::Condition::Pairing);
    CHECK_FALSE(tango.peer().paired());
    CHECK_FALSE(tango.go_pinned());

    tango.press_bravo();
    tango.run_for(2000);
    CHECK(tango.go_pinned());
    CHECK(tango.go.settings().bravo == PairedLink::kPeripheralAddress);
    CHECK(tango.peer().paired());
    CHECK(bravo::address_of(tango.bravo.product.settings().peer) == PairedLink::kCentralAddress);
    CHECK(tango.service().link() == go::BravoLink::Up);
    CHECK_FALSE(tango.peer().requested());
    CHECK(tango.bravo.lamp() != indication::Condition::Pairing);

    tango.run_for(5000);
    events::BluetoothAddress stored{};
    CHECK(stored_pin(tango.bravo.platform, stored));
    CHECK(stored == PairedLink::kCentralAddress);
}

TEST_CASE("bravo pairing: no press, no health: the Go gives up and neither side stores a pin") {
    Tango tango;
    tango.ask();
    tango.run_for(go::BravoService::kAnswerCeilingMs);
    CHECK(tango.service().step() == go::BravoStep::Refused);
    CHECK(tango.service().refusal() == go::BravoRefusal::NoAnswer);
    CHECK_FALSE(tango.go_pinned());
    CHECK_FALSE(tango.peer().paired());
    CHECK_FALSE(tango.peer().requested());
    events::BluetoothAddress stored{};
    CHECK_FALSE(stored_pin(tango.bravo.platform, stored));
}

// Opus's review: the held press outlasted kLongPressHoldMs, so it was never a press at all.
TEST_CASE("bravo pairing: a press held from before the request never pairs") {
    Tango tango;
    tango.go.show(tango.now_ms, go::Page::Bravo);
    tango.run_for(300);
    REQUIRE(tango.service().found_count() == 1);
    tango.go.press(tango.now_ms);
    const uint32_t down_ms = tango.now_ms;
    tango.bravo.platform.set_button_down(true);
    tango.go.press(tango.now_ms);
    tango.run_for(1200);
    REQUIRE(tango.peer().requested());
    REQUIRE(tango.bravo.lamp() == indication::Condition::Pairing);
    REQUIRE(tango.now_ms - down_ms < power::kLongPressHoldMs);
    tango.bravo.platform.set_button_down(false);
    tango.run_for(150);
    tango.run_for(2000);
    CHECK_FALSE(tango.go_pinned());
    CHECK_FALSE(tango.peer().paired());
    CHECK(tango.peer().requested());

    tango.press_bravo();
    tango.run_for(2000);
    CHECK(tango.go_pinned());
    CHECK(tango.peer().paired());
}

TEST_CASE("bravo pairing: an advisory outranks the request, and its press only dismisses") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    PairedLink go{rig.platform.link()};
    REQUIRE(go.connect());
    const auto run_for = [&](uint32_t span_ms) {
        for (uint32_t stepped = 0; stepped < span_ms; stepped += 10) {
            rig.step();
            go.pump();
            rig.now_ms += 10;
        }
    };
    rig.seconds_with_fix(20, 25000);
    rig.hear_aircraft(0x3C6586, 800);
    REQUIRE(go.write(complete_registry_frame(
                "{\"command\":\"peer_claim\",\"boot_epoch\":7,\"alarm_requested\":false,"
                "\"alarm_passes\":1,\"alarm_volume\":\"loud\",\"paired\":false}")) == Status::Ok);
    rig.push_fix(25000);
    run_for(1000);
    REQUIRE(rig.state().alarm_undismissed == traffic::Level::Advisory);
    REQUIRE(rig.product.peer().requested());
    CHECK(rig.lamp() == indication::Condition::Alarm);
    CHECK_FALSE(rig.product.button().pairing_armed());

    rig.press();
    run_for(100);
    CHECK(rig.state().alarm_undismissed == traffic::Level::None);
    CHECK(rig.product.peer().requested());
    const auto healths = [&go] {
        int count = 0;
        for (std::string frame; go.take(frame);)
            count += field(link_text(frame), "command") == "health" &&
                     field(link_text(frame), "paired") == "true";
        return count;
    };
    CHECK(healths() == 0);

    run_for(1000);
    CHECK(rig.lamp() == indication::Condition::Pairing);
    rig.press();
    run_for(100);
    CHECK_FALSE(rig.product.peer().requested());
    CHECK(healths() == 1);
}

// The Go pins on the first health, so a health lost after the press must leave Bravo unpinned.
TEST_CASE("bravo pairing: a pairing interrupted on the Go's side leaves no pin on either side") {
    Tango tango;
    tango.ask();
    tango.air.set_flow(Direction::ToCentral, Flow::Lost);
    tango.press_bravo();
    tango.run_for(go::BravoService::kAnswerCeilingMs);
    CHECK(tango.service().step() == go::BravoStep::Refused);
    CHECK_FALSE(tango.go_pinned());
    CHECK_FALSE(tango.peer().paired());

    tango.air.set_flow(Flow::Flowing);
    tango.run_for(bravo::Pairing::kOfferWindowMs);
    CHECK_FALSE(tango.peer().paired());
    events::BluetoothAddress stored{};
    CHECK_FALSE(stored_pin(tango.bravo.platform, stored));
}

TEST_CASE("bravo pairing: a pairing interrupted by Bravo's reboot leaves no pin on either side") {
    BravoRig rebooted{};
    Tango tango;
    tango.ask();
    tango.air.set_flow(Direction::ToCentral, Flow::Stalled);
    tango.press_bravo();

    REQUIRE(rebooted.setup() == Status::Ok);
    tango.air.reboot_peripheral(rebooted.platform.link());
    tango.air.set_flow(Flow::Flowing);
    tango.go.world = [&](uint32_t at_ms) {
        rebooted.now_ms = at_ms;
        rebooted.step();
        tango.air.pump();
    };
    tango.run_for(go::BravoService::kAnswerCeilingMs);
    CHECK_FALSE(tango.go_pinned());
    CHECK_FALSE(rebooted.product.peer().paired());
    CHECK_FALSE(rebooted.product.settings().peer.pinned);
}

// The Go read the health and pinned, then the link went before Bravo read its confirmation.
TEST_CASE("bravo pairing: a Go that pinned and lost the link completes Bravo's half on return") {
    Tango tango;
    tango.ask();
    tango.air.set_flow(Direction::ToPeripheral, Flow::Lost);
    tango.press_bravo();
    tango.run_for(500);
    REQUIRE(tango.go_pinned());
    CHECK_FALSE(tango.peer().paired());

    tango.air.drop();
    tango.air.set_flow(Flow::Flowing);
    tango.run_for(5000);
    CHECK(tango.service().link() == go::BravoLink::Up);
    CHECK(tango.peer().paired());
}

// Opus's review: a Go that retried inside the offer window paired with no new press.
TEST_CASE("bravo pairing: a Go that retries after a lost health needs a new press") {
    Tango tango;
    tango.ask();
    tango.air.set_flow(Direction::ToCentral, Flow::Lost);
    tango.press_bravo();
    tango.run_for(1000);
    REQUIRE_FALSE(tango.go_pinned());

    tango.go.tap(tango.now_ms);
    tango.air.set_flow(Flow::Flowing);
    tango.run_for(500);
    REQUIRE(tango.service().found_count() == 1);
    tango.go.press(tango.now_ms);
    tango.go.press(tango.now_ms);
    tango.run_for(3000);
    CHECK_FALSE(tango.go_pinned());
    CHECK_FALSE(tango.peer().paired());
    CHECK(tango.peer().requested());

    tango.press_bravo();
    tango.run_for(2000);
    CHECK(tango.go_pinned());
    CHECK(tango.peer().paired());
}

// #179 hands the alarm over only once both sides hold the pin.
TEST_CASE("bravo pairing: no peer is paired, and no health grants, before both sides confirm") {
    Tango tango;
    tango.ask();
    tango.air.set_flow(Direction::ToPeripheral, Flow::Stalled);
    tango.press_bravo();
    tango.run_for(1000);
    CHECK(tango.go_pinned());
    CHECK_FALSE(tango.peer().paired());

    tango.air.set_flow(Flow::Flowing);
    tango.run_for(1500);
    CHECK(tango.peer().paired());
    int healths = 0;
    for (const platform::host::Link::Frame& sent : tango.bravo.platform.link().sent) {
        if (sent.endpoint != events::Endpoint::Peer) continue;
        const std::string text = link_text(sent.bytes);
        if (field(text, "command") != "health") continue;
        healths++;
        CHECK(field(text, "alarm_granted") == "false");
    }
    CHECK(healths > 0);
}

TEST_CASE("bravo pairing: the pin survives Bravo's reboot, with its connection kept from phones") {
    std::optional<BravoRig> booted{};
    Tango tango;
    tango.pair_and_store();
    const std::string blob = stored_blob(tango.bravo.platform);
    REQUIRE_FALSE(blob.empty());

    BravoRig& rebooted = booted.emplace([&blob](platform::host::MeshtrackerX1& platform) {
        REQUIRE(platform.key_value().write("settings",
                                           reinterpret_cast<const uint8_t*>(blob.data()),
                                           blob.size()) == Status::Ok);
    });
    REQUIRE(rebooted.setup() == Status::Ok);
    CHECK(rebooted.product.peer().paired());
    rebooted.raise_link(2);
    rebooted.raise_link(3);
    rebooted.raise_link(4);
    CHECK(rebooted.platform.link().links() == 2);

    tango.air.reboot_peripheral(rebooted.platform.link());
    tango.go.world = [&](uint32_t at_ms) {
        rebooted.now_ms = at_ms;
        rebooted.step();
        tango.air.pump();
    };
    tango.air.drop();
    tango.run_for(5000);
    CHECK(tango.service().link() == go::BravoLink::Up);
    CHECK(rebooted.platform.link().links() == 3);
    CHECK_FALSE(rebooted.product.peer().requested());
}

// GPT's review: a Go that unpaired left Bravo pinned, its connection kept from phones.
TEST_CASE("bravo pairing: a Go that unpairs tells Bravo, which drops its pin and frees the slot") {
    Tango tango;
    tango.pair_and_store();
    REQUIRE(tango.peer().paired());

    tango.go.show(tango.now_ms, go::Page::Bravo);
    tango.go.press(tango.now_ms);
    REQUIRE(tango.go.product.screen().bravo_page().unpairing());
    tango.go.press(tango.now_ms);
    tango.run_for(1000);
    CHECK_FALSE(tango.go_pinned());
    CHECK_FALSE(tango.peer().paired());
    CHECK_FALSE(tango.peer().requested());
    CHECK_FALSE(tango.service().leaving());
    CHECK_FALSE(tango.go.platform.central().up());
    CHECK(tango.bravo.lamp() != indication::Condition::Pairing);

    tango.run_for(5000);
    events::BluetoothAddress stored{};
    CHECK_FALSE(stored_pin(tango.bravo.platform, stored));
    tango.bravo.raise_link(2);
    tango.bravo.raise_link(3);
    tango.bravo.raise_link(4);
    CHECK(tango.bravo.platform.link().links() == 3);
}

TEST_CASE("bravo pairing: a Go unpaired with Bravo away unpins it when it asks again") {
    Tango tango;
    tango.pair_and_store();
    tango.air.set_in_range(false);
    tango.air.drop();
    tango.run_for(500);
    tango.go.show(tango.now_ms, go::Page::Bravo);
    tango.go.press(tango.now_ms);
    tango.go.press(tango.now_ms);
    tango.run_for(500);
    REQUIRE_FALSE(tango.go_pinned());
    CHECK(tango.peer().paired());

    tango.air.set_in_range(true);
    tango.ask();
    CHECK_FALSE(tango.peer().paired());
    CHECK(tango.peer().requested());
}

// GPT's review: Bravo restarted after the Go read its health, and the Go read CONNECTED for ever.
TEST_CASE("bravo pairing: a Go pinned alone by Bravo's restart drops its pin and says so") {
    std::optional<BravoRig> booted{};
    Tango tango;
    tango.ask();
    tango.air.set_flow(Direction::ToPeripheral, Flow::Lost);
    tango.press_bravo();
    tango.run_for(500);
    REQUIRE(tango.go_pinned());
    REQUIRE_FALSE(tango.peer().paired());

    BravoRig& rebooted = booted.emplace();
    REQUIRE(rebooted.setup() == Status::Ok);
    tango.air.reboot_peripheral(rebooted.platform.link());
    tango.air.set_flow(Flow::Flowing);
    tango.go.world = [&](uint32_t at_ms) {
        rebooted.now_ms = at_ms;
        rebooted.step();
        tango.air.pump();
        CHECK(tango.service().link() != go::BravoLink::Up);
    };
    tango.air.drop();
    tango.run_for(5000);
    CHECK_FALSE(tango.go_pinned());
    CHECK_FALSE(rebooted.product.peer().paired());
    CHECK(tango.go.product.screen().noticing());
    CHECK(tango.go.product.screen().notice() == go::ScreenService::Notice::BravoForgot);
    for (uint16_t phone = 1; phone <= 3; phone++) tango.go.raise_link(phone);
    CHECK(tango.go.platform.link().links() == 3);
}

// GPT's review: a Go back after Bravo's offer lapsed kept a pin Bravo never stored.
TEST_CASE("bravo pairing: a Go back after Bravo's offer lapsed drops its pin and says so") {
    Tango tango;
    tango.ask();
    tango.air.set_flow(Direction::ToPeripheral, Flow::Lost);
    tango.press_bravo();
    tango.run_for(500);
    REQUIRE(tango.go_pinned());

    tango.air.set_in_range(false);
    tango.air.drop();
    tango.air.set_flow(Flow::Flowing);
    tango.run_for(bravo::Pairing::kOfferWindowMs + 1000);
    CHECK_FALSE(tango.peer().paired());
    tango.go.world = [&](uint32_t at_ms) {
        tango.bravo.now_ms = at_ms;
        tango.bravo.step();
        tango.air.pump();
        CHECK(tango.service().link() != go::BravoLink::Up);
    };
    tango.air.set_in_range(true);
    tango.run_for(5000);
    CHECK_FALSE(tango.go_pinned());
    CHECK_FALSE(tango.peer().paired());
    CHECK(tango.go.product.screen().notice() == go::ScreenService::Notice::BravoForgot);
}

TEST_CASE("bravo pairing: a Go that leaves its request takes the blinking lamp with it") {
    Tango tango;
    tango.ask();
    REQUIRE(tango.bravo.lamp() == indication::Condition::Pairing);
    tango.go.tap(tango.now_ms);
    tango.run_for(500);
    CHECK_FALSE(tango.peer().requested());
    CHECK(tango.bravo.lamp() != indication::Condition::Pairing);
}

TEST_CASE("bravo pairing: a second Go's request and a press replace the pin") {
    Tango tango;
    tango.pair_and_store();

    Rig second{};
    REQUIRE(second.setup() == Status::Ok);
    PairedLink second_air{tango.bravo.platform.link(), second.platform.central(), 2, kSecondGo};
    tango.go.world = [&](uint32_t at_ms) {
        tango.bravo.now_ms = at_ms;
        tango.bravo.step();
        tango.air.pump();
        second.platform.clock().set_milliseconds(at_ms);
        second.product.run_pass(at_ms);
        second_air.pump();
    };
    second.show(tango.now_ms, go::Page::Bravo);
    tango.run_for(300);
    REQUIRE(second.product.bravo().found_count() == 1);
    second.press(tango.now_ms);
    second.press(tango.now_ms);
    tango.run_for(2000);
    REQUIRE(tango.peer().requested());

    tango.press_bravo();
    tango.run_for(2000);
    CHECK(go::paired(second.settings()));
    CHECK(bravo::address_of(tango.bravo.product.settings().peer) == kSecondGo);
}
