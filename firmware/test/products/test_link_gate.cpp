// The two wires that decide whether the companion link can do anything at all: the flight state
// behind the gate, and the gesture that answers a prompt.
#include <cstring>
#include <string>

#include "core/events/link.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/platform.h"
#include "products/skyblip_go/pages/confirm.h"
#include "products/skyblip_go/product.h"
#include "runtime/tasks.h"
#include "test/support/confirm_glass.h"
#include "test/support/housekeeping_rig.h"

using namespace skyblip;

// The two dead wires, end to end on the real product. Neither had a caller: the
// gate was never told the flight state, so it refused everything forever, and
// nothing could ever call confirm(), so it refused everything twice over.

// Unknown is a unit with no fix since boot: it sends no position and grades no traffic.
TEST_CASE("product: the gate closes on the take-off the fix stream proved, not on a phone's word") {
    HousekeepingRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;

    rig.send("{\"command\":\"dfu\",\"version\":\"0.2.0+15\"}");
    rig.run(now_ms, 500);
    now_ms = 500;
    CHECK(rig.config().flight_state() == flight::FlightState::Unknown);
    CHECK(rig.config().pending() == comms::Pending::Dfu);

    rig.on_ground(now_ms);
    CHECK(rig.config().flight_state() == flight::FlightState::Ground);
    CHECK(rig.config().pending() == comms::Pending::Dfu);

    // And the same stream takes it away again: the transmitter and the update
    // lockout are reading one decision, not two.
    rig.airborne(now_ms);
    CHECK(rig.config().flight_state() == flight::FlightState::Airborne);
    CHECK(rig.config().pending() == comms::Pending::None);
    CHECK(rig.product.screen().prompt() == comms::Pending::None);
}

// The case the whole gesture was chosen for.
TEST_CASE("product: paging does not authorise a firmware upload") {
    HousekeepingRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    rig.on_ground(now_ms);

    // A pilot pages through the screens, on a device with nothing pending.
    rig.tap(now_ms);
    REQUIRE(rig.product.screen().page() == go::Page::Nearby);

    // Long enough for the question to have reached the glass.
    rig.send("{\"command\":\"dfu\",\"version\":\"0.2.0+15\"}");
    rig.run(now_ms, now_ms + 3000);
    now_ms += 3000;
    REQUIRE(rig.config().pending() == comms::Pending::Dfu);

    // The prompt owns the glass: the pad neither turns the page nor answers it.
    rig.tap(now_ms);
    rig.run(now_ms, now_ms + common::ConfirmGesture::kDoublePressWindowMs + 200);
    now_ms += common::ConfirmGesture::kDoublePressWindowMs + 200;
    CHECK_FALSE(rig.config().upload_allowed());
    CHECK(rig.config().pending() == comms::Pending::Dfu);
    CHECK(rig.product.screen().page() == go::Page::Nearby);

    // The button at a prompt is the answer, and one press alone refuses.
    rig.press(now_ms);
    rig.run(now_ms, now_ms + common::ConfirmGesture::kDoublePressWindowMs + 200);
    now_ms += common::ConfirmGesture::kDoublePressWindowMs + 200;
    CHECK_FALSE(rig.config().upload_allowed());
    CHECK(rig.config().pending() == comms::Pending::None);
    CHECK(rig.product.screen().mode() == go::Mode::Page);

    // With the prompt gone, the same tap pages again.
    rig.tap(now_ms);
    CHECK(rig.product.screen().page() == go::Page::SixPack);
}

TEST_CASE("product: two presses on the ground are what open the upload window") {
    HousekeepingRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    rig.on_ground(now_ms);

    rig.send("{\"command\":\"dfu\",\"version\":\"0.2.0+15\"}");
    rig.run(now_ms, now_ms + 3000);
    now_ms += 3000;
    REQUIRE(rig.config().pending() == comms::Pending::Dfu);
    REQUIRE_FALSE(rig.config().upload_allowed());

    rig.double_press(now_ms);
    rig.run(now_ms, now_ms + 200);
    now_ms += 200;
    CHECK(rig.config().upload_allowed());
    CHECK(rig.config().pending() == comms::Pending::None);
    CHECK(rig.product.screen().prompt() == comms::Pending::None);
    // The page it was on is the page it comes back to.
    CHECK(rig.product.screen().page() == go::Page::Radar);

    // The same two presses, airborne. Fail closed: the window shuts on takeoff
    // and no gesture reopens it.
    rig.airborne(now_ms);
    CHECK_FALSE(rig.config().upload_allowed());
    rig.send("{\"command\":\"dfu\",\"version\":\"0.2.0+15\"}");
    rig.run(now_ms, now_ms + 3000);
    now_ms += 3000;
    rig.double_press(now_ms);
    rig.run(now_ms, now_ms + 200);
    CHECK_FALSE(rig.config().upload_allowed());
}

TEST_CASE("product: the panel names the operation while it waits, and stops cycling pages") {
    HousekeepingRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    rig.on_ground(now_ms);

    rig.send("{\"command\":\"power_off\"}");
    rig.run(now_ms, now_ms + 6000);
    now_ms += 6000;
    REQUIRE(rig.product.screen().prompt() == comms::Pending::PowerOff);

    go::ConfirmSnapshot expect;
    expect.title = comms::pending_title(comms::Pending::PowerOff);
    expect.detail = comms::pending_detail(comms::Pending::PowerOff);
    expect.seconds_left = seconds_left_on(rig.product.screen().framebuffer());
    CHECK(expect.seconds_left > 0);
    go::Glass expected;
    go::draw_confirm(expected, expect);
    CHECK(std::memcmp(rig.product.screen().framebuffer().data(), expected.data(),
                      go::Glass::kBytes) == 0);

    // It reached the glass, not just the buffer: the pilot being asked can see
    // the question before the button can answer it.
    CHECK(rig.platform.chips().epd.framebuffer().count_black() == expected.count_black());
}

TEST_CASE("product: the panel counts down the seconds the prompt has left, once a second") {
    HousekeepingRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    rig.on_ground(now_ms);

    rig.send("{\"command\":\"power_off\"}");
    rig.run(now_ms, now_ms + 3000);
    now_ms += 3000;
    const uint32_t first = seconds_left_on(rig.platform.chips().epd.framebuffer());
    REQUIRE(first > 25);

    for (uint32_t elapsed_s = 1; elapsed_s <= 5; elapsed_s++) {
        rig.run(now_ms, now_ms + 1000);
        now_ms += 1000;
        CAPTURE(elapsed_s);
        CHECK(seconds_left_on(rig.platform.chips().epd.framebuffer()) == first - elapsed_s);
    }
}

TEST_CASE("product: a prompt nobody answers expires, and the device is not powered off") {
    HousekeepingRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    rig.on_ground(now_ms);

    rig.send("{\"command\":\"power_off\"}");
    rig.run(now_ms, now_ms + 1000);
    now_ms += 1000;
    REQUIRE(rig.product.screen().prompt() == comms::Pending::PowerOff);

    rig.run(now_ms, now_ms + comms::kPromptCeilingMs + 1000);
    now_ms += comms::kPromptCeilingMs + 1000;
    CHECK(rig.config().pending() == comms::Pending::None);
    CHECK(rig.product.screen().prompt() == comms::Pending::None);
    CHECK_FALSE(rig.product.shutdown().going_down());

    // And the panel is back on the page the pilot left it on.
    CHECK(rig.product.screen().page() == go::Page::Radar);
    rig.tap(now_ms);
    CHECK(rig.product.screen().page() == go::Page::Nearby);
}

// I: a receiver with a poisoned almanac takes twenty minutes to fix and a pilot
// reads that as a broken device. The driver has always been able to throw the
// stored orbit data away; until this, nothing could ask it to. It costs the next
// fix, so it sits behind the same confirmation as a firmware upload.
TEST_CASE(
    "product: a confirmed gnss_cold_start reaches the receiver, and an unconfirmed one does not") {
    HousekeepingRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    rig.on_ground(now_ms);
    const uint32_t restarts_before = rig.platform.chips().gnss.restarts;

    rig.send("{\"command\":\"gnss_cold_start\"}");
    rig.run(now_ms, now_ms + 3000);
    now_ms += 3000;
    // Staged and waiting for the device's own gesture: the phone cannot spend a
    // pilot's next fix by itself.
    REQUIRE(rig.config().pending() == comms::Pending::GnssColdStart);
    CHECK(rig.platform.chips().gnss.restarts == restarts_before);

    rig.double_press(now_ms);
    rig.run(now_ms, now_ms + 2000);
    now_ms += 2000;
    CHECK(rig.config().pending() == comms::Pending::None);
    CHECK(rig.platform.chips().gnss.restarts == restarts_before + 1);
    // A cold start, not a hot one: the point is to discard the almanac.
    CHECK(rig.platform.chips().gnss.last_restart_kind >= 2);
    // And it is spent once, not once per pass.
    rig.run(now_ms, now_ms + 5000);
    CHECK(rig.platform.chips().gnss.restarts == restarts_before + 1);
}

// Airborne, the same request is refused at the door: losing the fix in flight is
// the one moment the device must not do this.
TEST_CASE("product: gnss_cold_start is refused in flight") {
    HousekeepingRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    rig.airborne(now_ms);
    const uint32_t restarts_before = rig.platform.chips().gnss.restarts;

    rig.send("{\"command\":\"gnss_cold_start\"}");
    rig.run(now_ms, now_ms + 3000);
    now_ms += 3000;
    CHECK(rig.config().pending() == comms::Pending::None);
    rig.double_press(now_ms);
    rig.run(now_ms, now_ms + 2000);
    CHECK(rig.platform.chips().gnss.restarts == restarts_before);
}

// A unit with no fix since boot is Unknown, and the radar menu changes the same values without one.
TEST_CASE("product: a set with no fix since boot is confirmed by two presses and stored") {
    HousekeepingRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;

    rig.send("{\"command\":\"set\",\"callsign\":\"F-JXYZ\"}");
    rig.run(now_ms, now_ms + 3000);
    now_ms += 3000;
    REQUIRE(rig.config().flight_state() == flight::FlightState::Unknown);
    REQUIRE(rig.config().pending() == comms::Pending::Set);
    REQUIRE(rig.product.screen().prompt() == comms::Pending::Set);

    rig.double_press(now_ms);
    rig.run(now_ms, now_ms + 2000);
    CHECK(rig.config().pending() == comms::Pending::None);
    CHECK(std::strcmp(rig.product.settings().callsign, "F-JXYZ") == 0);
    uint8_t blob[64];
    size_t count = 0;
    REQUIRE(rig.platform.key_value().read("settings", blob, sizeof(blob), count) == Status::Ok);
    go::Settings stored{};
    REQUIRE(go::from_blob(blob, count, stored) == Status::Ok);
    CHECK(std::strcmp(stored.callsign, "F-JXYZ") == 0);
}

// Checked on every pass: a prompt takes the glass and both controls, alarm dismissal included.
TEST_CASE("product: a set sent in flight never puts a prompt over the radar") {
    HousekeepingRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    rig.airborne(now_ms);
    REQUIRE(rig.config().flight_state() == flight::FlightState::Airborne);

    bool prompted = false;
    for (int i = 0; i < 3; i++) {
        rig.send("{\"command\":\"set\",\"alarm_volume\":1}");
        for (const uint32_t until = now_ms + 1000; now_ms <= until; now_ms += 50) {
            rig.platform.clock().set_milliseconds(now_ms);
            rig.product.run_pass(now_ms);
            prompted = prompted || rig.product.screen().prompt() != comms::Pending::None;
        }
    }
    CHECK_FALSE(prompted);
    CHECK(rig.config().pending() == comms::Pending::None);
    CHECK(rig.product.screen().page() == go::Page::Radar);
    CHECK(rig.product.settings().alarm_volume == go::AlarmVolume::Loud);
}

// Pass by pass, because config ran before ownship and saw the take-off one pass late.
TEST_CASE("product: no prompt stands on the glass on any pass the latch says airborne") {
    struct Staged {
        const char* request;
        bool fixed_first;
    };
    for (const Staged staged : {Staged{"{\"command\":\"set\",\"alarm_volume\":1}", false},
                                Staged{"{\"command\":\"power_off\"}", true}}) {
        const std::string request = staged.request;
        CAPTURE(request);
        HousekeepingRig rig;
        REQUIRE(rig.setup() == Status::Ok);
        uint32_t now_ms = 0;
        if (staged.fixed_first) rig.on_ground(now_ms);
        rig.send(staged.request);
        rig.run(now_ms, now_ms + 3000);
        now_ms += 3050;
        REQUIRE(rig.product.screen().prompt() != comms::Pending::None);

        int airborne_passes = 0;
        int prompted_passes = 0;
        for (int fix = 0; fix < 14; fix++) {
            rig.push_fix(/*speed_mm_s=*/50000, /*altitude_msl_m=*/1200);
            for (int pass = 0; pass < 10; pass++, now_ms += 50) {
                rig.platform.clock().set_milliseconds(now_ms);
                rig.product.run_pass(now_ms);
                if (rig.state().flight.confirmed_state != flight::FlightState::Airborne) continue;
                airborne_passes++;
                const bool config_heard =
                    rig.config().flight_state() == flight::FlightState::Airborne;
                if (rig.product.screen().prompt() != comms::Pending::None ||
                    (config_heard && rig.config().pending() != comms::Pending::None))
                    prompted_passes++;
            }
        }
        REQUIRE(airborne_passes > 0);
        CHECK(prompted_passes == 0);
    }
}

// One pass: the first airborne fix brings an advisory, and the press that lands with it confirms
// nothing.
TEST_CASE("product: the pass the first fix says airborne takes a staged set off an advisory") {
    HousekeepingRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.send("{\"command\":\"set\",\"alarm_volume\":1}");
    rig.run(0, 3000);
    REQUIRE(rig.config().flight_state() == flight::FlightState::Unknown);
    REQUIRE(rig.product.screen().prompt() == comms::Pending::Set);

    const auto pass = [&](uint32_t at_ms) {
        rig.platform.clock().set_milliseconds(at_ms);
        rig.product.run_pass(at_ms);
    };
    const auto button = [&](bool down, uint32_t at_ms) {
        rig.product.bus().input.push(events::ContactEvent{events::Contact::Button, down, at_ms});
    };
    button(true, 3010);
    pass(3010);
    button(false, 3110);
    pass(3110);
    button(true, 3160);
    pass(3160);
    REQUIRE(rig.config().pending() == comms::Pending::Set);

    model::AircraftObservation target{};
    target.address = 0x424242;
    target.address_mapping_table = 6;
    target.position_valid = true;
    target.latitude_1e7 = 5000;
    target.flight_state = static_cast<uint8_t>(flight::FlightState::Airborne);
    target.at_ms = 3260;
    target.received.at_s = 3;
    target.received.into_ms = 260;
    rig.state().traffic.update(target, 3);
    rig.push_fix(/*speed_mm_s=*/50000, /*altitude_msl_m=*/0);
    button(false, 3260);
    pass(3260);

    REQUIRE(rig.state().flight.confirmed_state == flight::FlightState::Airborne);
    REQUIRE(rig.state().alarm_undismissed == traffic::Level::Advisory);
    CHECK(rig.product.screen().prompt() == comms::Pending::None);
    CHECK(rig.product.settings().alarm_volume == go::AlarmVolume::Loud);

    // The next pass, config hears the take-off and drops what the glass already dropped.
    pass(3310);
    CHECK(rig.config().pending() == comms::Pending::None);
}
