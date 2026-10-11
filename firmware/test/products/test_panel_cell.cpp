// The cell on the glass: the word the ring wears, the words under the mark, and the note the next
// boot reads.
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

    rig.platform.battery().millivolts = power::kCriticalMv - 100;
    rig.run(t, t + 8000);
    t += 8000;
    REQUIRE(rig.state().power.level == power::PowerLevel::Critical);
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
    CHECK(rig.platform.system_power().cell_on_glass() == power::CellOnGlass::Flat);

    // The button stays armed, so a press after it reaches the lockout and not a dead unit.
    rig.platform.system_power().system_off();
    CHECK(rig.platform.system_power().order_of(power::PowerDownStep::WakePinArmed) ==
          power::kPowerDownStepCount - 1);
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
    CHECK(rig.platform.system_power().cell_on_glass() == power::CellOnGlass::Flat);

    // Never a percentage: it would be the reading the device died at, standing unchanged
    // through the whole charge that follows.
    CHECK_FALSE(reads_in(parked, "%", 0, 0, 200, 199, 2));
}

// A frame is seconds of panel rail, and the cell paying for it is the flat one.
TEST_CASE("product: a refused boot pushes no frame the glass is already wearing") {
    Rig rig;
    rig.platform.battery().millivolts = power::kBootLockoutMv - 1;
    rig.platform.system_power().glass_cell = power::CellOnGlass::Flat;
    REQUIRE(rig.setup() == Status::Ok);
    REQUIRE(rig.product.refused_frame() == power::RefusedFrame::Leave);
    rig.sleep_again();
    CHECK(rig.platform.chips().epd.present_count == 0);
}

// The cutoff left FLAT BATTERY on the glass, and the press that follows needs no other answer.
TEST_CASE("product: a press after a cutoff is refused and leaves the flat frame where it is") {
    Rig dying;
    REQUIRE(dying.setup() == Status::Ok);
    dying.run(0, 2000);
    dying.platform.battery().millivolts = 3100;
    dying.run(2000, 20000);
    REQUIRE(dying.product.ready_to_power_off());
    REQUIRE(dying.platform.system_power().cell_on_glass() == power::CellOnGlass::Flat);

    Rig pressed;
    pressed.platform.system_power().glass_cell = dying.platform.system_power().cell_on_glass();
    pressed.platform.system_power().dark_flat = dying.platform.system_power().went_dark_flat();
    pressed.platform.battery().millivolts = power::kBootLockoutMv - 1;
    pressed.platform.system_power().causes = power::ResetCause::LowPowerWake;
    pressed.platform.board_gpio().button_down = true;
    REQUIRE(pressed.setup() == Status::Ok);
    REQUIRE(pressed.product.boot_path() == power::BootPath::SleepAgain);
    CHECK(pressed.product.refused_frame() == power::RefusedFrame::Leave);
    pressed.platform.board_gpio().button_down = false;
    pressed.sleep_again();
    CHECK(pressed.platform.chips().epd.present_count == 0);
    CHECK(pressed.platform.system_power().cell_on_glass() == power::CellOnGlass::Flat);
    CHECK(pressed.platform.system_power().went_dark_flat());

    pressed.platform.system_power().system_off();
    CHECK(pressed.platform.system_power().order_of(power::PowerDownStep::WakePinArmed) ==
          power::kPowerDownStepCount - 1);
}

// SENSE is a level detect: armed under the finger it refused, the wake pin fires at once.
TEST_CASE("product: a refused press goes dark only once the button has come up") {
    Rig rig;
    rig.platform.battery().millivolts = power::kBootLockoutMv - 1;
    rig.platform.system_power().causes = power::ResetCause::LowPowerWake;
    rig.platform.board_gpio().button_down = true;
    REQUIRE(rig.setup() == Status::Ok);
    REQUIRE(rig.product.refused_frame() == power::RefusedFrame::FlatCell);

    bool dark = false;
    uint32_t t = 0;
    for (; t <= go::kRefusalParkCeilingMs + 2000; t += 50) {
        rig.platform.clock().set_millis(t);
        dark = dark || rig.product.park_refusal(t);
    }
    CHECK_FALSE(dark);
    CHECK(rig.platform.chips().epd.present_count == 1);

    rig.platform.board_gpio().button_down = false;
    CHECK_FALSE(rig.product.park_refusal(t));
    CHECK_FALSE(rig.product.park_refusal(t + power::kReleaseSettleMs - 1));
    CHECK(rig.product.park_refusal(t + power::kReleaseSettleMs));
}

// The lockout spares external power, so the cable and a press are the way out of a flat cell.
TEST_CASE("product: a press on the cable after a cutoff starts the device and clears the word") {
    Rig dying;
    REQUIRE(dying.setup() == Status::Ok);
    dying.run(0, 2000);
    dying.platform.battery().millivolts = 3100;
    dying.run(2000, 20000);
    REQUIRE(dying.product.ready_to_power_off());

    Rig plugged;
    plugged.platform.system_power().glass_cell = dying.platform.system_power().cell_on_glass();
    plugged.platform.battery().millivolts = power::kBootLockoutMv - 1;
    plugged.platform.battery().external_power = true;
    plugged.platform.system_power().causes = power::ResetCause::LowPowerWake;
    plugged.platform.board_gpio().button_down = true;
    REQUIRE(plugged.setup() == Status::Ok);
    CHECK(plugged.product.boot_path() == power::BootPath::Run);
    CHECK(plugged.state().started);

    plugged.platform.board_gpio().button_down = false;
    plugged.run(0, 5000);
    CHECK_FALSE(plugged.product.shutdown().going_down());
    CHECK_FALSE(
        reads_in(plugged.platform.chips().epd.framebuffer(), "FLAT BATTERY", 0, 0, 200, 199, 2));
    CHECK(plugged.platform.system_power().cell_on_glass() == power::CellOnGlass::None);
}

// The factory bootloader swallows a charger wake: one that got through would get the armed frame.
TEST_CASE("product: a charger wake past the bootloader takes the word back off the glass") {
    Rig rig;
    rig.platform.battery().millivolts = power::kBootLockoutMv - 1;
    rig.platform.battery().external_power = true;
    rig.platform.system_power().glass_cell = power::CellOnGlass::Flat;
    rig.platform.system_power().causes =
        power::ResetCause::LowPowerWake | power::ResetCause::UsbVbus;
    REQUIRE(rig.setup() == Status::Ok);
    REQUIRE(rig.product.boot_path() == power::BootPath::SleepAgain);
    rig.sleep_again();

    go::Glass expected;
    expected.clear(true);
    ui::draw_wordmark(expected, go::kGlassW / 2, go::kGlassH / 2);
    CHECK(rig.platform.chips().epd.framebuffer().count_black() == expected.count_black());
    CHECK(rig.platform.system_power().cell_on_glass() == power::CellOnGlass::None);

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

// The lamp's warning dies with the rails, so the glass carries it to whoever picks the device up.
TEST_CASE("product: a switch-off on a critical cell asks for the charger under the mark") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, 2000);
    rig.platform.battery().millivolts = power::kCriticalMv - 100;
    rig.run(2000, 10000);
    REQUIRE(rig.state().power.level == power::PowerLevel::Critical);

    rig.product.shutdown().request(power::ShutdownReason::LongPress, 10000);
    rig.run(10000, 20000);
    REQUIRE(rig.product.ready_to_power_off());

    const go::Glass& parked = rig.platform.chips().epd.framebuffer();
    CHECK(reads_in(parked, "CHARGE BATTERY", 0, 130, 200, 199, 2));
    CHECK_FALSE(reads_in(parked, "FLAT BATTERY", 0, 0, 200, 199, 2));
    CHECK(rig.platform.system_power().cell_on_glass() == power::CellOnGlass::Low);
    CHECK_FALSE(rig.platform.system_power().went_dark_flat());
}

// The first step already winks the lamp red, so the glass has to carry that word too.
TEST_CASE(
    "product: a cell past the knee wears BAT in the ring and asks for the charger at switch-off") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, 2000);
    rig.platform.battery().millivolts = power::kLowMv - 50;
    rig.run(2000, 10000);
    REQUIRE(rig.state().power.level == power::PowerLevel::Low);
    CHECK(reads_in(rig.platform.chips().epd.framebuffer(), "BAT", 40, 120, 160, 160, 2));

    rig.product.shutdown().request(power::ShutdownReason::LongPress, 10000);
    rig.run(10000, 20000);
    REQUIRE(rig.product.ready_to_power_off());
    CHECK(reads_in(rig.platform.chips().epd.framebuffer(), "CHARGE BATTERY", 0, 130, 200, 199, 2));
    CHECK(rig.platform.system_power().cell_on_glass() == power::CellOnGlass::Low);
}

// A cell reading 5% sat on the critical step, and one sample above it parked the bare mark.
TEST_CASE(
    "product: a switch-off on a cell resting at the critical step still asks for the charger") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, 2000);
    rig.platform.battery().millivolts = power::kCriticalMv - 10;
    rig.run(2000, 10000);
    REQUIRE(rig.state().power.level == power::PowerLevel::Critical);

    rig.platform.battery().millivolts = power::kCriticalMv + 5;
    rig.run(10000, 11500);
    REQUIRE(rig.state().power.level == power::PowerLevel::Critical);

    rig.product.shutdown().request(power::ShutdownReason::LongPress, 11500);
    rig.run(11500, 21500);
    REQUIRE(rig.product.ready_to_power_off());
    CHECK(reads_in(rig.platform.chips().epd.framebuffer(), "CHARGE BATTERY", 0, 130, 200, 199, 2));
}

// A cable in says the charge is already coming, so the mark alone is the frame.
TEST_CASE("product: a switch-off on the cable wears the plain mark, however low the cell") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, 2000);
    rig.platform.battery().millivolts = power::kCriticalMv - 100;
    rig.platform.battery().external_power = true;
    rig.run(2000, 10000);

    rig.product.shutdown().request(power::ShutdownReason::LongPress, 10000);
    rig.run(10000, 20000);
    REQUIRE(rig.product.ready_to_power_off());
    CHECK_FALSE(reads_in(rig.platform.chips().epd.framebuffer(), "BATTERY", 0, 0, 200, 199, 2));
    CHECK(rig.platform.system_power().cell_on_glass() == power::CellOnGlass::None);
}

// The support half of a flat cell: the boot the cable lets run says so, once.
TEST_CASE("product: the press on the cable after a flat cell names it on the self-test page") {
    Rig refused;
    refused.platform.battery().millivolts = power::kBootLockoutMv - 1;
    REQUIRE(refused.setup() == Status::Ok);
    refused.sleep_again();
    CHECK(refused.platform.system_power().went_dark_flat());

    Rig pressed;
    pressed.platform.system_power().glass_cell = refused.platform.system_power().cell_on_glass();
    pressed.platform.system_power().dark_flat = refused.platform.system_power().went_dark_flat();
    pressed.platform.battery().millivolts = power::kBootLockoutMv - 1;
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
    REQUIRE(dying.platform.system_power().cell_on_glass() == power::CellOnGlass::None);
    CHECK(dying.platform.system_power().went_dark_flat());
}
