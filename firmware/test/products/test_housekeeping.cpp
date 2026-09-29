// The housekeeping a device needs to survive being switched on, wired up to the
// real product on the host platform: the way out (long press, cutoff, link), the
// gauge that acts rather than reports, and the loop's half of the watchdog.
// Nothing here is mocked below the services - the same board, the same service
// list, the same part models the silicon build uses.
#include <cstring>

#include "core/events/link.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/platform.h"
#include "products/skyblip_go/pages/confirm.h"
#include "products/skyblip_go/product.h"
#include "runtime/tasks.h"
#include "test/support/housekeeping_rig.h"

using namespace skyblip;

// D4: the way out. The radio is asked to sleep, the panel is parked, and only
// then are the rails allowed to drop - and not while the button is still down.

TEST_CASE("product: a long press parks the radio and the panel, then asks for the rails") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.run(0, 1000);
    t = 1000;
    REQUIRE(rig.product.board().rf().sleeps() == 0);

    rig.hold_button(t, power::kLongPressMs + 200);
    CHECK(rig.product.shutdown().reason() == power::ShutdownReason::LongPress);
    CHECK(rig.product.shutdown().phase() == power::ShutdownPhase::Parking);
    // A receiver armed through most of every second is what flattens the pack,
    // so it is the first thing told to stop.
    CHECK(rig.product.board().rf().sleeps() == 1);
    CHECK_FALSE(rig.product.screen().powered());
    CHECK(rig.platform.chips().epd.last_full);

    // Still held: the rails must not go, or a level-sensed wake pin brings the
    // device straight back up.
    rig.hold_button(t, power::kParkMs + 1000);
    CHECK_FALSE(rig.product.ready_to_power_off());
    // The park frame has had its seconds by now, so the panel is asleep before the rails go.
    CHECK_FALSE(rig.platform.chips().epd.powered);

    rig.hold_button(t, power::kReleaseSettleMs + 200, /*down=*/false);
    CHECK(rig.product.ready_to_power_off());
}

// Rails cut mid-frame leave the ink half-driven, and in the sun it goes on developing.
TEST_CASE("product: the rails wait for the park frame, not only for the park window") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.run(0, 1000);
    t = 1000;

    rig.hold_button(t, power::kLongPressMs + 200);
    REQUIRE(rig.product.shutdown().phase() == power::ShutdownPhase::Parking);
    rig.platform.chips().epd.busy_stuck = true;

    rig.hold_button(t, power::kParkMs, /*down=*/false);
    rig.hold_button(t, power::kReleaseSettleMs + 200, /*down=*/false);
    CHECK_FALSE(rig.product.ready_to_power_off());
    CHECK(rig.platform.chips().epd.powered);

    rig.platform.chips().epd.busy_stuck = false;
    rig.hold_button(t, 200, /*down=*/false);
    CHECK(rig.product.ready_to_power_off());
    CHECK_FALSE(rig.platform.chips().epd.powered);
}

// The unit as flashed paged under the thumb, two seconds before the rails went.
TEST_CASE("product: the hold that switches the device off does not page first") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.run(0, 1000);
    t = 1000;
    const go::Page page = rig.product.screen().page();

    rig.hold_button(t, power::kLongPressMs + 200);
    CHECK(rig.product.shutdown().reason() == power::ShutdownReason::LongPress);
    CHECK(rig.product.screen().page() == page);

    rig.hold_button(t, 300, /*down=*/false);
    CHECK(rig.product.screen().page() == page);
    // Nor does it open the settings on the way out: a hold is never a press.
    CHECK(rig.product.screen().mode() == go::Mode::Page);
}

// D4 over the link: the same road, from a phone instead of a thumb.

TEST_CASE("product: a confirmed power_off over the link parks the device like a long press") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.on_ground(t);
    rig.run(t, 1000);

    rig.send("{\"cmd\":\"power_off\"}");
    rig.run(1000, 1500);
    // Asking is not doing: the request is still waiting for the confirmation
    // the device demands, and nothing has moved.
    CHECK(rig.config().pending() == comms::Pending::PowerOff);
    CHECK_FALSE(rig.product.shutdown().going_down());
    CHECK(rig.product.board().rf().sleeps() == 0);

    rig.config().confirm();
    rig.run(1500, 2000);
    CHECK(rig.product.shutdown().reason() == power::ShutdownReason::LinkRequest);
    CHECK(rig.product.shutdown().phase() == power::ShutdownPhase::Parking);
    CHECK(rig.product.board().rf().sleeps() == 1);
    CHECK_FALSE(rig.product.screen().powered());

    // The latch is consumed, not left standing: a device that came back up with
    // it set would power itself off again.
    CHECK_FALSE(rig.config().power_off_requested());

    // No button was ever down, so there is no release to wait for.
    rig.run(2000, 2000 + power::kParkMs + power::kReleaseSettleMs + 500);
    CHECK(rig.product.ready_to_power_off());
}

TEST_CASE("product: an unconfirmed power_off over the link turns nothing off") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.on_ground(t);
    rig.send("{\"cmd\":\"power_off\"}");
    rig.run(t, 5000);
    CHECK_FALSE(rig.product.shutdown().going_down());
    CHECK(rig.product.screen().powered());
    CHECK(rig.product.board().rf().sleeps() == 0);

    // And one that was cancelled stays cancelled.
    rig.config().cancel();
    rig.run(5000, 8000);
    CHECK_FALSE(rig.product.shutdown().going_down());

    // Airborne, the device refuses to arm it at all: a link request cannot land
    // an aircraft.
    Rig flying;
    REQUIRE(flying.setup() == Status::Ok);
    uint32_t ft = 0;
    flying.airborne(ft);
    flying.send("{\"cmd\":\"power_off\"}");
    flying.run(ft, ft + 2000);
    ft += 2000;
    CHECK(flying.config().pending() == comms::Pending::None);
    flying.config().confirm();
    flying.run(ft, ft + 2000);
    CHECK_FALSE(flying.product.shutdown().going_down());
}

TEST_CASE("product: a page tap is not a power-off") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.tap(t);
    rig.run(t, t + 2000);
    CHECK(rig.product.screen().page() == go::Page::Nearby);
    CHECK_FALSE(rig.product.shutdown().going_down());
    CHECK(rig.product.board().rf().sleeps() == 0);
}

// D3: the acting half of the gauge, wired up.

TEST_CASE("product: a cell at its cutoff takes the device down on its own") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, 2000);
    REQUIRE_FALSE(rig.product.power().cutoff());

    rig.platform.battery().millivolts = 3100;
    rig.run(2000, 8000);
    CHECK(rig.product.power().cutoff());
    CHECK(rig.product.shutdown().going_down());
    CHECK(rig.product.shutdown().reason() == power::ShutdownReason::LowBattery);
    CHECK(rig.product.board().rf().sleeps() == 1);

    rig.run(8000, 20000);
    CHECK(rig.product.ready_to_power_off());
}

// The ratchet: switched off at the cutoff, pressed back on, flattened a bit more.
TEST_CASE("product: a cell too flat to run refuses the boot instead of spending it") {
    Rig rig;
    rig.platform.battery().millivolts = power::kBootLockoutMv - 1;
    CHECK(rig.setup() == Status::Ok);
    CHECK(rig.product.boot_path() == power::BootPath::SleepAgain);
    CHECK(rig.platform.chips().epd.present_count == 0);
    CHECK_FALSE(rig.state().started);

    Rig on_charge;
    on_charge.platform.battery().millivolts = power::kBootLockoutMv - 1;
    on_charge.platform.battery().external_power = true;
    CHECK(on_charge.setup() == Status::Ok);
    CHECK(on_charge.product.boot_path() == power::BootPath::Run);

    Rig healthy;
    healthy.platform.battery().millivolts = power::kBootLockoutMv;
    CHECK(healthy.setup() == Status::Ok);
    CHECK(healthy.product.boot_path() == power::BootPath::Run);
}

// A unit that failed its self test runs no services, so nothing else watches it.
TEST_CASE("product: a device that cannot fly still switches itself off on a flat cell") {
    constexpr ports::Capabilities kNoGnss = static_cast<ports::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(ports::Capability::Gnss));
    Rig rig(kNoGnss);
    REQUIRE(rig.setup() == Status::Down);
    REQUIRE_FALSE(rig.product.flyable());

    rig.platform.battery().millivolts = 3100;
    rig.run(0, 8000);
    CHECK(rig.product.power().cutoff());
    CHECK(rig.state().power.battery.valid);
    CHECK(rig.product.shutdown().reason() == power::ShutdownReason::LowBattery);

    rig.run(8000, 20000);
    CHECK(rig.product.ready_to_power_off());
}

TEST_CASE("product: critical comes before the cutoff, and a floating sense never acts") {
    Rig warned;
    REQUIRE(warned.setup() == Status::Ok);
    warned.platform.battery().millivolts = 3400;
    warned.run(0, 8000);
    // The level is published on the bus, which is where the status page reads
    // it: nothing downstream compares millivolts a second time.
    CHECK(warned.state().power.level == power::PowerLevel::Critical);
    CHECK_FALSE(warned.product.power().cutoff());
    CHECK_FALSE(warned.product.shutdown().going_down());

    // An unconnected divider drifts near zero. It must not switch a device off
    // in someone's hand.
    Rig floating;
    REQUIRE(floating.setup() == Status::Ok);
    floating.platform.battery().millivolts = 200;
    floating.run(0, 20000);
    CHECK(floating.product.power().implausible_samples() > 3);
    CHECK_FALSE(floating.product.power().cutoff());
    CHECK_FALSE(floating.product.shutdown().going_down());
}

// D1: the loop's half of the watchdog, at product scale.

TEST_CASE("product: the loop feeds the watchdog while it is flying and through a shutdown") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    for (uint32_t t = 0; t <= 3 * runtime::kTaskWatchdogMs; t += 100) {
        rig.platform.clock().set_millis(t);
        rig.product.step(t);
        CHECK(rig.product.may_feed_watchdog(t));
    }

    // A device that is deliberately going down is doing what it was told: a
    // held button must not turn a power-off into a watchdog reboot.
    uint32_t t = 3 * runtime::kTaskWatchdogMs;
    rig.hold_button(t, power::kLongPressMs + 200);
    REQUIRE(rig.product.shutdown().going_down());
    rig.hold_button(t, 2 * runtime::kTaskWatchdogMs);
    CHECK(rig.product.may_feed_watchdog(t));
}
