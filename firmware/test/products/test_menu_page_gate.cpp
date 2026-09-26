// What editing on the panel cannot spend: the prompt a double press answers, the hold that
// switches the device off, and the refusal a phone gets in the air.
#include <string>

#include "core/timing/durable_write.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/input/gesture.h"
#include "products/skyblip_go/pages/confirm.h"
#include "products/skyblip_go/pages/menu.h"
#include "products/skyblip_go/settings.h"
#include "test/support/menu_rig.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

// core/flight decides what a fix stream means, and the companion link's gate
// reads what it decided. Standing still is on the ground; five seconds of
// believed motion is airborne.
void on_ground(Rig& rig, uint32_t& t) {
    push_solution(rig, 0, 0);
    rig.run(t, t + 200);
    t += 200;
}

void airborne(Rig& rig, uint32_t& t) {
    for (int i = 0; i < 14; i++) {
        push_solution(rig, 50000, 1200);
        rig.run(t, t + 500);
        t += 500;
    }
}

void hold_button(Rig& rig, uint32_t& t, uint32_t ms, bool down = true) {
    rig.platform.board_gpio().button_down = down;
    const uint32_t until = t + ms;
    for (; t <= until; t += 50) {
        rig.platform.clock().set_millis(t);
        rig.product.step(t);
    }
}

}  // namespace

// The half of this the gate cares most about: editing must not be able to spend
// the gesture that authorises, and must not be able to reach the way out.

TEST_CASE("product: a prompt takes the page, and the taps already in flight cannot answer it") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    on_ground(rig, t);
    open_menu(rig, t);
    focus_on(rig, t, go::MenuRow::Volume);
    const uint8_t volume = rig.settings().alarm_volume;

    // A phone asks to overwrite the firmware while the pilot is tapping a value
    // up. The prompt takes the glass at once.
    rig.send("{\"cmd\":\"dfu\"}");
    rig.run(t, t + 300);
    t += 300;
    REQUIRE(rig.product.screen().prompt() == comms::Pending::Dfu);

    // The thumb has not caught up: it keeps tapping at the rhythm that was
    // stepping the volume, through the moment the question appears. Not one of
    // those taps authorises anything, and none of them reaches the value
    // either - a press stream that began before the question cannot answer it.
    for (int i = 0; i < 6; i++) {
        rig.press(t);
        CHECK_FALSE(rig.product.config().config().upload_allowed());
    }
    rig.run(t, t + 200);
    t += 200;
    CHECK(rig.product.config().config().pending() == comms::Pending::Dfu);
    CHECK_FALSE(rig.product.config().config().upload_allowed());
    CHECK(rig.settings().alarm_volume == volume);

    // The pilot stops, and the question is on the glass where it can be read.
    rig.run(t, t + 4000);
    t += 4000;
    go::ConfirmSnapshot expect;
    expect.title = comms::pending_title(comms::Pending::Dfu);
    expect.detail = comms::pending_detail(comms::Pending::Dfu);
    expect.timeout_s = comms::kConfirmWindowMs / 1000;
    go::Glass prompt_page;
    go::draw_confirm(prompt_page, expect);
    CHECK(std::memcmp(rig.product.screen().framebuffer().data(), prompt_page.data(),
                      go::Glass::kBytes) == 0);
    CHECK(rig.platform.chips().epd.framebuffer().count_black() == prompt_page.count_black());

    // Only now is it answerable, and only by the gesture: physical presence,
    // deliberately, after the reading.
    rig.press(t);
    rig.press(t);
    rig.run(t, t + 200);
    t += 200;
    CHECK(rig.product.config().config().upload_allowed());

    // Back on the rows, at the top: the pilot was reading something else in between.
    CHECK(rig.product.screen().mode() == go::Mode::Menu);
    CHECK(rig.product.screen().editor().focus() == go::MenuRow::AircraftType);
}

TEST_CASE("product: a long press in the middle of an edit still switches the device off") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    open_menu(rig, t);
    focus_on(rig, t, go::MenuRow::Volume);
    const uint8_t volume = rig.settings().alarm_volume;

    hold_button(rig, t, power::kLongPressMs + 300);
    CHECK(rig.product.shutdown().reason() == power::ShutdownReason::LongPress);
    CHECK(rig.product.shutdown().phase() == power::ShutdownPhase::Parking);
    CHECK_FALSE(rig.product.screen().powered());

    // A hold makes no press edge at all, so the row the pilot was standing on is untouched.
    CHECK(rig.settings().alarm_volume == volume);
    go::Settings stored{};
    CHECK_FALSE(stored_settings(rig, stored));
}

TEST_CASE("product: the volume can be turned up in the air, where a phone is refused") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    airborne(rig, t);
    REQUIRE(rig.product.config().config().flight_state() == flight::FlightState::Airborne);

    // The companion app cannot: an unattended phone rewriting the whole struct
    // mid-flight is what that rule is for.
    rig.send("{\"cmd\":\"set\",\"alarm_volume\":5}");
    rig.run(t, t + 300);
    t += 300;
    CHECK(rig.product.config().config().pending() == comms::Pending::None);
    CHECK(rig.settings().alarm_volume == 3);

    // The pilot, on the panel, can. Presence is the thing the phone lacks, and
    // an alarm that is too quiet under a headset is a thing you find out about
    // in the air.
    open_menu(rig, t);
    focus_on(rig, t, go::MenuRow::Volume);
    change(rig, t);
    change(rig, t);
    run_past_the_write_settle(rig, t);
    CHECK(rig.settings().alarm_volume == 5);

    go::Settings stored{};
    REQUIRE(stored_settings(rig, stored));
    CHECK(stored.alarm_volume == 5);
}
