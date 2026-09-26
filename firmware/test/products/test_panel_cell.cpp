// A flat cell on the glass: the word the ring wears, the reason under the mark, and the note the
// next boot reads.
#include "doctest/doctest.h"
#include "products/skyblip_go/pages/boot.h"
#include "test/support/glass_text.h"
#include "test/support/product_rig.h"
#include "ui/widgets/wordmark.h"

using namespace skyblip;

// One decision, in core/power: a cable is not a low cell, so the ring has nothing to say.
TEST_CASE("product: a cable takes the cell's word off the radar, and no charge climbs there") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.run(t, t + 2000);
    t += 2000;

    rig.platform.battery().millivolts = power::kLowWarnMv - 100;
    rig.run(t, t + 8000);
    t += 8000;
    REQUIRE(rig.state().power.level == power::PowerLevel::Low);
    CHECK(reads_in(rig.platform.chips().epd.framebuffer(), "BAT", 40, 120, 160, 160, 2));

    rig.platform.battery().external_power = true;
    rig.run(t, t + 8000);
    CHECK(rig.state().power.level == power::PowerLevel::Normal);
    CHECK_FALSE(reads_in(rig.platform.chips().epd.framebuffer(), "BAT", 0, 0, 200, 199, 2));
    CHECK_FALSE(reads_in(rig.platform.chips().epd.framebuffer(), "%", 0, 0, 200, 199, 2));
}

// Nobody pressed anything, so the frame is the only thing that can say why the device stopped.
TEST_CASE("product: a cell that takes the device down names itself on the way out") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, 2000);

    rig.platform.battery().millivolts = 3100;
    rig.run(2000, 20000);
    REQUIRE(rig.product.shutdown().reason() == power::ShutdownReason::LowBattery);
    REQUIRE(rig.product.ready_to_power_off());

    const go::Glass& parked = rig.platform.chips().epd.framebuffer();
    CHECK(reads_in(parked, "FLAT BATTERY", 10, 130, 190, 199, 2));
    CHECK_FALSE(reads_in(parked, "%", 0, 0, 200, 199, 2));
    CHECK(rig.platform.system_power().flat_on_glass());

    // The press after it gets nothing: the frame is the answer and the wake pin is never armed.
    rig.platform.system_power().system_off(
        power::button_wake_after(rig.product.shutdown().reason(), rig.platform.external_power()));
    CHECK(rig.platform.system_power().order_of(power::PowerDownStep::WakePinArmed) == -1);
}

// The other road to the same empty cell: a winter on a shelf, which runs no shutdown at all.
TEST_CASE("product: a boot refused on a flat cell writes the reason under the mark") {
    Rig rig;
    rig.platform.battery().millivolts = power::kBootLockoutMv - 1;
    REQUIRE(rig.setup() == Status::Ok);
    REQUIRE(rig.product.boot_path() == power::BootPath::SleepAgain);
    rig.sleep_again();

    const go::Glass& parked = rig.platform.chips().epd.framebuffer();
    CHECK(rig.platform.chips().epd.present_count == 1);
    CHECK(rig.platform.chips().epd.last_full);
    CHECK_FALSE(rig.platform.chips().epd.powered);
    CHECK(reads_in(parked, "FLAT BATTERY", 10, 130, 190, 199, 2));
    CHECK(rig.platform.system_power().flat_on_glass());

    // Never a percentage: it would be the reading the device died at, standing unchanged
    // through the whole charge that follows.
    CHECK_FALSE(reads_in(parked, "%", 0, 0, 200, 199, 2));
}

// A frame is seconds of panel rail, and the cell paying for it is the flat one.
TEST_CASE("product: a refused boot pushes no frame the glass is already wearing") {
    Rig rig;
    rig.platform.battery().millivolts = power::kBootLockoutMv - 1;
    rig.platform.system_power().flat_glass = true;
    REQUIRE(rig.setup() == Status::Ok);
    REQUIRE(rig.product.refused_frame() == power::RefusedFrame::Leave);
    rig.sleep_again();
    CHECK(rig.platform.chips().epd.present_count == 0);
}

// The cable re-arms the button, so the mark is the whole instruction again.
TEST_CASE("product: the charger that wakes a flat device takes the word back off the glass") {
    Rig rig;
    rig.platform.battery().millivolts = power::kBootLockoutMv - 1;
    rig.platform.battery().external_power = true;
    rig.platform.system_power().flat_glass = true;
    rig.platform.system_power().causes =
        power::ResetCause::LowPowerWake | power::ResetCause::UsbVbus;
    REQUIRE(rig.setup() == Status::Ok);
    REQUIRE(rig.product.boot_path() == power::BootPath::SleepAgain);
    rig.sleep_again();

    go::Glass expected;
    expected.clear(true);
    ui::draw_wordmark(expected, go::kGlassW / 2, go::kGlassH / 2);
    CHECK(rig.platform.chips().epd.framebuffer().count_black() == expected.count_black());
    CHECK_FALSE(rig.platform.system_power().flat_on_glass());

    // And the cable wiggle after it costs nothing: the glass already says the right thing.
    Rig again;
    again.platform.battery().millivolts = power::kBootLockoutMv - 1;
    again.platform.battery().external_power = true;
    again.platform.system_power().causes =
        power::ResetCause::LowPowerWake | power::ResetCause::UsbVbus;
    REQUIRE(again.setup() == Status::Ok);
    again.sleep_again();
    CHECK(again.platform.chips().epd.present_count == 0);
}

// The whole road, on the one bit that survives the rails: the cutoff writes it, the cable reads it.
TEST_CASE("product: the cable after a cutoff puts the mark back and arms the button") {
    Rig dying;
    REQUIRE(dying.setup() == Status::Ok);
    dying.run(0, 2000);
    dying.platform.battery().millivolts = 3100;
    dying.run(2000, 20000);
    REQUIRE(dying.product.ready_to_power_off());

    Rig plugged;
    plugged.platform.system_power().flat_glass = dying.platform.system_power().flat_on_glass();
    plugged.platform.battery().millivolts = power::kBootLockoutMv - 1;
    plugged.platform.battery().external_power = true;
    plugged.platform.system_power().causes =
        power::ResetCause::LowPowerWake | power::ResetCause::UsbVbus;
    REQUIRE(plugged.setup() == Status::Ok);
    REQUIRE(plugged.product.refused_frame() == power::RefusedFrame::Wordmark);
    plugged.sleep_again();

    const go::Glass& parked = plugged.platform.chips().epd.framebuffer();
    CHECK_FALSE(reads_in(parked, "FLAT BATTERY", 0, 0, 200, 199, 2));
    CHECK_FALSE(plugged.platform.system_power().flat_on_glass());
    CHECK(power::button_wake_after_refusal(plugged.product.boot_cell()) ==
          power::ButtonWake::Armed);
}

// The support half of a flat cell: the glass forgets it on the cable, the boot that runs does not.
TEST_CASE("product: the boot after a flat cell names it, though the cable took the word away") {
    Rig refused;
    refused.platform.battery().millivolts = power::kBootLockoutMv - 1;
    REQUIRE(refused.setup() == Status::Ok);
    refused.sleep_again();
    CHECK(refused.platform.system_power().went_dark_flat());

    Rig plugged;
    plugged.platform.system_power().flat_glass = refused.platform.system_power().flat_on_glass();
    plugged.platform.system_power().dark_flat = refused.platform.system_power().went_dark_flat();
    plugged.platform.battery().millivolts = power::kBootLockoutMv - 1;
    plugged.platform.battery().external_power = true;
    plugged.platform.system_power().causes =
        power::ResetCause::LowPowerWake | power::ResetCause::UsbVbus;
    REQUIRE(plugged.setup() == Status::Ok);
    plugged.sleep_again();
    REQUIRE_FALSE(plugged.platform.system_power().flat_on_glass());
    CHECK(plugged.platform.system_power().went_dark_flat());

    Rig pressed;
    pressed.platform.system_power().dark_flat = plugged.platform.system_power().went_dark_flat();
    pressed.platform.battery().external_power = true;
    pressed.platform.system_power().causes = power::ResetCause::LowPowerWake;
    pressed.platform.board_gpio().button_down = true;
    REQUIRE(pressed.setup() == Status::Ok);
    REQUIRE(pressed.product.boot_path() == power::BootPath::Run);
    CHECK(pressed.product.went_dark_flat());
    CHECK(reads_in(pressed.product.boot_page(), "WAS FLAT", 0, 150, 200, 199));
    CHECK_FALSE(pressed.platform.system_power().went_dark_flat());
    pressed.send("{\"cmd\":\"status\"}");
    pressed.run(0, 200);
    CHECK(pressed.last_on(events::Endpoint::Config).find("\"went_dark_flat\":true") !=
          std::string::npos);
}

TEST_CASE("product: a cutoff leaves the note for the next boot, an ordinary boot has none") {
    Rig dying;
    REQUIRE(dying.setup() == Status::Ok);
    dying.run(0, 2000);
    dying.platform.battery().millivolts = 3100;
    dying.run(2000, 20000);
    REQUIRE(dying.product.ready_to_power_off());
    CHECK(dying.platform.system_power().went_dark_flat());

    Rig ordinary;
    REQUIRE(ordinary.setup() == Status::Ok);
    CHECK_FALSE(ordinary.product.went_dark_flat());
    CHECK_FALSE(reads_in(ordinary.product.boot_page(), "WAS FLAT", 0, 0, 200, 199));
}

// The note is the cell's, not the frame's: a panel held off in the heat parks nothing.
TEST_CASE("product: a cutoff too hot to park a frame still notes the flat cell") {
    constexpr ports::Capabilities kWithDie = static_cast<ports::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) |
        static_cast<uint32_t>(ports::Capability::DieTemperature));
    Rig dying{kWithDie};
    dying.platform.die_temperature().hold(go::ScreenService::kHoldAboveDeciCelsius + 10);
    REQUIRE(dying.setup() == Status::Ok);
    dying.run(0, 2000);
    dying.platform.battery().millivolts = 3100;
    dying.run(2000, 20000);
    REQUIRE(dying.product.ready_to_power_off());
    REQUIRE_FALSE(dying.platform.system_power().flat_on_glass());
    CHECK(dying.platform.system_power().went_dark_flat());
}
