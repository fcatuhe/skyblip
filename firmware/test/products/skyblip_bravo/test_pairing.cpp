// A whole Go pairs with a whole Bravo over the simulated link, and neither keeps half a pin (#192).
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "test/support/adsb_frames.h"
#include "test/support/bravo_rig.h"
#include "test/support/log_transfer.h"
#include "test/support/paired_link.h"
#include "test/support/paired_products.h"
#include "test/support/product_rig.h"
#include "test/support/registry_frames.h"

using namespace skyblip;

namespace {

using Direction = PairedLink::Direction;
using Flow = PairedLink::Flow;

constexpr events::BluetoothAddress kSecondGo{1, {0x11, 0x22, 0x33, 0x44, 0x55, 0xE8}};

bool stored_pin(platform::host::MeshtrackerX1& platform, events::BluetoothAddress& out) {
    const std::string blob = PairedProducts::stored_in(platform.key_value());
    bravo::Settings stored{};
    if (bravo::from_blob(reinterpret_cast<const uint8_t*>(blob.data()), blob.size(), stored) !=
        Status::Ok)
        return false;
    out = bravo::address_of(stored.peer);
    return stored.peer.pinned;
}

// A Go asking to pair, played by hand: its claim reaches Bravo, and Bravo's frames come back.
struct Asked {
    BravoRig rig{};
    PairedLink go{rig.platform.link()};

    Asked() {
        REQUIRE(rig.setup() == Status::Ok);
        rig.seconds_with_fix(2);
        REQUIRE(go.connect());
        REQUIRE(go.write(complete_registry_frame(
                    "{\"command\":\"peer_claim\",\"boot_epoch\":7,\"alarm_requested\":false,"
                    "\"paired\":false}")) == Status::Ok);
        run_for(30);
        REQUIRE(rig.product.peer().requested());
    }

    void run_for(uint32_t span_ms) {
        for (uint32_t stepped = 0; stepped < span_ms; stepped += 10) {
            rig.step();
            go.pump();
            rig.now_ms += 10;
        }
    }

    bool offered() {
        bool offered = false;
        for (std::string frame; go.take(frame);) {
            const std::string text = link_text(frame);
            offered =
                offered || (field(text, "command") == "health" && field(text, "paired") == "true");
        }
        return offered;
    }
};

uint32_t pairing_arms_at_ms() {
    Asked dry;
    for (int pass = 0; pass < 200 && !dry.rig.product.button().pairing_armed(); pass++)
        dry.run_for(10);
    REQUIRE(dry.rig.product.button().pairing_armed());
    return dry.rig.now_ms - 10;
}

}  // namespace

TEST_CASE("bravo pairing: a Go's request blinks the lamp, and one press pairs both sides") {
    PairedProducts tango;
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
    PairedProducts tango;
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
    PairedProducts tango;
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
                "\"paired\":false}")) == Status::Ok);
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
    PairedProducts tango;
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
    PairedProducts tango;
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
    PairedProducts tango;
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
    PairedProducts tango;
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

// #245's rule on the pairing press: the debounce publishes a down a pass or more after it began.
TEST_CASE("bravo pairing: a press that began before the gesture armed never pairs") {
    const uint32_t armed_ms = pairing_arms_at_ms();

    Asked asked;
    asked.run_for(armed_ms - 20 - asked.rig.now_ms);
    asked.rig.platform.set_button_down(true);
    asked.run_for(100);
    asked.rig.platform.set_button_down(false);
    asked.run_for(100);
    CHECK(asked.rig.product.peer().requested());
    CHECK_FALSE(asked.offered());

    asked.run_for(1000);
    asked.rig.press();
    asked.run_for(100);
    CHECK_FALSE(asked.rig.product.peer().requested());
    CHECK(asked.offered());
}

// Bravo's writer into the Go's reader: no case ran the two apart from #193's stub.
TEST_CASE("bravo pairing: an aircraft Bravo hears reaches the Go with Bravo's time and callsign") {
    constexpr uint32_t kAircraft = 0x3C6586;
    PairedProducts tango;
    tango.pair_and_store();
    const auto second = [&tango] {
        tango.go.push_timed_fix(0, BravoRig::kOwnAltitudeMm / 1000);
        tango.bravo.push_fix();
        tango.run_for(950);
        tango.go.utc_offset_s++;
        tango.bravo.utc_offset_s++;
    };
    for (int i = 0; i < 3; i++) second();
    tango.bravo.hear_aircraft(kAircraft, 2000);
    tango.bravo.hear(adsb_identification(kAircraft, 5, 4, 1, "AFR123"));
    second();
    second();

    const bus::State& bravo = tango.bravo.state();
    const int heard = bravo.traffic.find(settings::kIcaoAddressMappingTable, kAircraft);
    REQUIRE(heard >= 0);
    const bus::State& go = tango.go.state();
    const int folded = go.traffic.find(settings::kIcaoAddressMappingTable, kAircraft);
    REQUIRE(folded >= 0);
    const model::AircraftObservation& sent = bravo.traffic.at(heard)->observation;
    const model::AircraftObservation& read = go.traffic.at(folded)->observation;
    CHECK(read.source == model::Source::Adsb);
    CHECK(read.received.at_s == sent.received.at_s);
    CHECK(read.latitude_1e7 == sent.latitude_1e7);
    CHECK(read.pressure_altitude_ft == sent.pressure_altitude_ft);
    const char* callsign = go.callsigns.find(settings::kIcaoAddressMappingTable, kAircraft);
    REQUIRE(callsign != nullptr);
    CHECK(std::string(callsign) == "AFR123");
}

// #179 hands the alarm over only once both sides hold the pin, to a Go+ with a fix.
TEST_CASE("bravo pairing: no peer is paired, and no health grants, before both sides confirm") {
    PairedProducts tango;
    const std::function<void(uint32_t)> world = tango.go.world;
    uint32_t next_fix_ms = 0;
    tango.go.world = [&](uint32_t at_ms) {
        if (at_ms >= next_fix_ms) {
            tango.go.push_timed_fix(0, 0);
            tango.bravo.push_fix();
            tango.go.utc_offset_s++;
            tango.bravo.utc_offset_s++;
            next_fix_ms = at_ms + 1000;
        }
        world(at_ms);
    };
    const auto healths = [&tango](size_t from, size_t to) {
        std::vector<std::string> out;
        const auto& sent = tango.bravo.platform.link().sent;
        for (size_t i = from; i < to; i++) {
            if (sent[i].endpoint != events::Endpoint::Peer) continue;
            const std::string text = link_text(sent[i].bytes);
            if (field(text, "command") == "health") out.push_back(text);
        }
        return out;
    };

    tango.ask();
    tango.air.set_flow(Direction::ToPeripheral, Flow::Stalled);
    tango.press_bravo();
    tango.run_for(1000);
    CHECK(tango.go_pinned());
    CHECK_FALSE(tango.peer().paired());
    const size_t confirmed_at = tango.bravo.platform.link().sent.size();
    const std::vector<std::string> before = healths(0, confirmed_at);
    CHECK_FALSE(before.empty());
    for (const std::string& health : before) CHECK(field(health, "alarm_granted") == "false");

    tango.air.set_flow(Flow::Flowing);
    tango.run_for(3000);
    CHECK(tango.peer().paired());
    const std::vector<std::string> after =
        healths(confirmed_at, tango.bravo.platform.link().sent.size());
    REQUIRE_FALSE(after.empty());
    CHECK(field(after.back(), "alarm_granted") == "true");
}

TEST_CASE("bravo pairing: the pin survives Bravo's reboot, with its connection kept from phones") {
    std::optional<BravoRig> booted{};
    PairedProducts tango;
    tango.pair_and_store();
    const std::string blob = PairedProducts::stored_in(tango.bravo.platform.key_value());
    REQUIRE_FALSE(blob.empty());

    BravoRig& rebooted = booted.emplace([&blob](platform::host::MeshtrackerX1& platform) {
        PairedProducts::keep(platform.key_value(), blob);
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
    PairedProducts tango;
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
    PairedProducts tango;
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
    PairedProducts tango;
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
    PairedProducts tango;
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
    PairedProducts tango;
    tango.ask();
    REQUIRE(tango.bravo.lamp() == indication::Condition::Pairing);
    tango.go.tap(tango.now_ms);
    tango.run_for(500);
    CHECK_FALSE(tango.peer().requested());
    CHECK(tango.bravo.lamp() != indication::Condition::Pairing);
}

TEST_CASE("bravo pairing: a second Go's request and a press replace the pin") {
    PairedProducts tango;
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
