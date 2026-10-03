// The two wires that decide whether the companion link can do anything at all: the flight state
// behind the gate, and the gesture that answers a prompt.
#include <cstring>

#include "core/events/link.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/platform.h"
#include "products/skyblip_go/pages/confirm.h"
#include "products/skyblip_go/product.h"
#include "runtime/tasks.h"
#include "test/support/housekeeping_rig.h"

using namespace skyblip;

// The two dead wires, end to end on the real product. Neither had a caller: the
// gate was never told the flight state, so it refused everything forever, and
// nothing could ever call confirm(), so it refused everything twice over.

TEST_CASE("product: the gate opens on the ground the fix stream proved, not on a phone's word") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;

    // No solution yet: the device does not know where it is, so it is not on
    // the ground, so it authorises nothing. This is the state a bench device
    // was stuck in - Unknown, forever.
    rig.send("{\"cmd\":\"dfu\",\"version\":\"0.2.0+15\"}");
    rig.run(t, 500);
    CHECK(rig.config().flight_state() == flight::FlightState::Unknown);
    CHECK(rig.config().pending() == comms::Pending::None);
    t = 500;

    rig.on_ground(t);
    CHECK(rig.config().flight_state() == flight::FlightState::Ground);
    rig.send("{\"cmd\":\"dfu\",\"version\":\"0.2.0+15\"}");
    rig.run(t, t + 500);
    t += 500;
    CHECK(rig.config().pending() == comms::Pending::Dfu);

    // And the same stream takes it away again: the transmitter and the update
    // lockout are reading one decision, not two.
    rig.airborne(t);
    CHECK(rig.config().flight_state() == flight::FlightState::Airborne);
    CHECK(rig.config().pending() == comms::Pending::None);
    CHECK(rig.product.screen().prompt() == comms::Pending::None);
}

// The case the whole gesture was chosen for.
TEST_CASE("product: paging does not authorise a firmware upload") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.on_ground(t);

    // A pilot pages through the screens, on a device with nothing pending.
    rig.tap(t);
    REQUIRE(rig.product.screen().page() == go::Page::Nearby);

    // Long enough for the question to have reached the glass.
    rig.send("{\"cmd\":\"dfu\",\"version\":\"0.2.0+15\"}");
    rig.run(t, t + 3000);
    t += 3000;
    REQUIRE(rig.config().pending() == comms::Pending::Dfu);

    // The prompt owns the glass: the pad neither turns the page nor answers it.
    rig.tap(t);
    rig.run(t, t + go::ConfirmGesture::kDoublePressMs + 200);
    t += go::ConfirmGesture::kDoublePressMs + 200;
    CHECK_FALSE(rig.config().upload_allowed());
    CHECK(rig.config().pending() == comms::Pending::Dfu);
    CHECK(rig.product.screen().page() == go::Page::Nearby);

    // The button at a prompt is the answer, and one press alone refuses.
    rig.press(t);
    rig.run(t, t + go::ConfirmGesture::kDoublePressMs + 200);
    t += go::ConfirmGesture::kDoublePressMs + 200;
    CHECK_FALSE(rig.config().upload_allowed());
    CHECK(rig.config().pending() == comms::Pending::None);
    CHECK(rig.product.screen().mode() == go::Mode::Page);

    // With the prompt gone, the same tap pages again.
    rig.tap(t);
    CHECK(rig.product.screen().page() == go::Page::SixPack);
}

TEST_CASE("product: two presses on the ground are what open the upload window") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.on_ground(t);

    rig.send("{\"cmd\":\"dfu\",\"version\":\"0.2.0+15\"}");
    rig.run(t, t + 3000);
    t += 3000;
    REQUIRE(rig.config().pending() == comms::Pending::Dfu);
    REQUIRE_FALSE(rig.config().upload_allowed());

    rig.double_press(t);
    rig.run(t, t + 200);
    t += 200;
    CHECK(rig.config().upload_allowed());
    CHECK(rig.config().pending() == comms::Pending::None);
    CHECK(rig.product.screen().prompt() == comms::Pending::None);
    // The page it was on is the page it comes back to.
    CHECK(rig.product.screen().page() == go::Page::Radar);

    // The same two presses, airborne. Fail closed: the window shuts on takeoff
    // and no gesture reopens it.
    rig.airborne(t);
    CHECK_FALSE(rig.config().upload_allowed());
    rig.send("{\"cmd\":\"dfu\",\"version\":\"0.2.0+15\"}");
    rig.run(t, t + 3000);
    t += 3000;
    rig.double_press(t);
    rig.run(t, t + 200);
    CHECK_FALSE(rig.config().upload_allowed());
}

TEST_CASE("product: the panel names the operation while it waits, and stops cycling pages") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.on_ground(t);

    rig.send("{\"cmd\":\"power_off\"}");
    rig.run(t, t + 6000);
    t += 6000;
    REQUIRE(rig.product.screen().prompt() == comms::Pending::PowerOff);

    go::ConfirmSnapshot expect;
    expect.title = comms::pending_title(comms::Pending::PowerOff);
    expect.detail = comms::pending_detail(comms::Pending::PowerOff);
    expect.timeout_s = comms::kConfirmWindowMs / 1000;
    go::Glass expected;
    go::draw_confirm(expected, expect);
    CHECK(std::memcmp(rig.product.screen().framebuffer().data(), expected.data(),
                      go::Glass::kBytes) == 0);

    // It reached the glass, not just the buffer: the pilot being asked can see
    // the question before the button can answer it.
    CHECK(rig.platform.chips().epd.framebuffer().count_black() == expected.count_black());
}

TEST_CASE("product: a prompt nobody answers expires, and the device is not powered off") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.on_ground(t);

    rig.send("{\"cmd\":\"power_off\"}");
    rig.run(t, t + 1000);
    t += 1000;
    REQUIRE(rig.product.screen().prompt() == comms::Pending::PowerOff);

    rig.run(t, t + comms::kConfirmWindowMs + 1000);
    t += comms::kConfirmWindowMs + 1000;
    CHECK(rig.config().pending() == comms::Pending::None);
    CHECK(rig.product.screen().prompt() == comms::Pending::None);
    CHECK_FALSE(rig.product.shutdown().going_down());

    // And the panel is back on the page the pilot left it on.
    CHECK(rig.product.screen().page() == go::Page::Radar);
    rig.tap(t);
    CHECK(rig.product.screen().page() == go::Page::Nearby);
}

// I: a receiver with a poisoned almanac takes twenty minutes to fix and a pilot
// reads that as a broken device. The driver has always been able to throw the
// stored orbit data away; until this, nothing could ask it to. It costs the next
// fix, so it sits behind the same confirmation as a firmware upload.
TEST_CASE("product: a confirmed gnss_cold reaches the receiver, and an unconfirmed one does not") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.on_ground(t);
    const uint32_t restarts_before = rig.platform.chips().gnss.restarts;

    rig.send("{\"cmd\":\"gnss_cold\"}");
    rig.run(t, t + 3000);
    t += 3000;
    // Staged and waiting for the device's own gesture: the phone cannot spend a
    // pilot's next fix by itself.
    REQUIRE(rig.config().pending() == comms::Pending::GnssCold);
    CHECK(rig.platform.chips().gnss.restarts == restarts_before);

    rig.double_press(t);
    rig.run(t, t + 2000);
    t += 2000;
    CHECK(rig.config().pending() == comms::Pending::None);
    CHECK(rig.platform.chips().gnss.restarts == restarts_before + 1);
    // A cold start, not a hot one: the point is to discard the almanac.
    CHECK(rig.platform.chips().gnss.last_restart_kind >= 2);
    // And it is spent once, not once per pass.
    rig.run(t, t + 5000);
    CHECK(rig.platform.chips().gnss.restarts == restarts_before + 1);
}

// Airborne, the same request is refused at the door: losing the fix in flight is
// the one moment the device must not do this.
TEST_CASE("product: gnss_cold is refused in flight") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.airborne(t);
    const uint32_t restarts_before = rig.platform.chips().gnss.restarts;

    rig.send("{\"cmd\":\"gnss_cold\"}");
    rig.run(t, t + 3000);
    t += 3000;
    CHECK(rig.config().pending() == comms::Pending::None);
    rig.double_press(t);
    rig.run(t, t + 2000);
    CHECK(rig.platform.chips().gnss.restarts == restarts_before);
}
