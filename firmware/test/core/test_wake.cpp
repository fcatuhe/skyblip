// The reset cause and the cell at boot decide whether a wake gets a running device.
#include <initializer_list>
#include <string>

#include "core/power/cutoff.h"
#include "core/power/reset_reason.h"
#include "core/power/shutdown.h"
#include "core/power/wake.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::power;

// Which reset the device came out of. Seven causes, one register, and the nRF52
// latches them: after a watchdog bite that was followed by a soft reset, both
// bits are set and only one of the two is the diagnosis.

TEST_CASE("reset: a fault cause outranks the benign one that came with it") {
    CHECK(classify(ResetCause::Watchdog | ResetCause::Software) == ResetReason::Watchdog);
    CHECK(classify(ResetCause::Lockup | ResetCause::Pin) == ResetReason::Lockup);
    CHECK(classify(ResetCause::Brownout | ResetCause::PowerOn) == ResetReason::Brownout);
    CHECK(classify(ResetCause::Watchdog | ResetCause::Lockup) == ResetReason::Watchdog);
}

TEST_CASE("reset: every cause the silicon can raise has one name") {
    CHECK(classify(ResetCause::None) == ResetReason::Unknown);
    CHECK(classify(ResetCause::PowerOn) == ResetReason::PowerOn);
    CHECK(classify(ResetCause::Pin) == ResetReason::Pin);
    CHECK(classify(ResetCause::Brownout) == ResetReason::Brownout);
    CHECK(classify(ResetCause::Software) == ResetReason::Software);
    CHECK(classify(ResetCause::Watchdog) == ResetReason::Watchdog);
    CHECK(classify(ResetCause::Lockup) == ResetReason::Lockup);
    CHECK(classify(ResetCause::LowPowerWake) == ResetReason::LowPowerWake);
    CHECK(classify(ResetCause::UsbVbus) == ResetReason::ChargerWake);
    CHECK(classify(ResetCause::Debug) == ResetReason::Debug);

    // A reason with no name reaches the panel as a blank, which is the one
    // outcome a self-test page must never produce.
    for (uint8_t i = 0; i <= static_cast<uint8_t>(ResetReason::Debug); i++) {
        const char* name = to_string(static_cast<ResetReason>(i));
        CHECK(name[0] != '\0');
    }
}

// D. The wake cause and the cell decide the boot path. On this board a VBUS event
// is a wake source, so plugging a charger into a device that was switched off
// brings the SoC up - and a device that boots in a flight bag because someone
// plugged it in arrives flat.

static BootCell healthy(uint16_t cell_mv = 3900) {
    return BootCell{cell_mv, /*valid=*/true, /*external_power=*/false};
}

// The one the item exists for.
TEST_CASE("wake: a charger plugged into a sleeping device does not switch it on") {
    const ResetCause charger = ResetCause::LowPowerWake | ResetCause::UsbVbus;
    CHECK(boot_path(charger, /*button_down=*/false, healthy()) == BootPath::SleepAgain);
    // And the page a bench eye reads names it, rather than calling it a wake.
    CHECK(classify(charger) == ResetReason::ChargerWake);
    CHECK(std::string(to_string(ResetReason::ChargerWake)) == "CHARGER WAKE");
}

// The same thumb as any press, so the same hold before it is a device.
TEST_CASE("wake: a pilot holding the button while plugging in is held to the wake hold") {
    const ResetCause charger = ResetCause::LowPowerWake | ResetCause::UsbVbus;
    CHECK(boot_path(charger, /*button_down=*/true, healthy()) == BootPath::AwaitHold);
}

// The failure that would be catastrophic and silent: a unit that refuses to boot
// the first time a cell is connected. On the nRF52840 RESETREAS is all-zero after
// a power-on or brown-out reset, so the wake bit is what separates the two, and
// the rule needs BOTH bits rather than either.
TEST_CASE("wake: no cause that could be a first power-on refuses a boot by itself") {
    CHECK(boot_path(ResetCause::PowerOn, false, healthy()) == BootPath::Run);
    CHECK(boot_path(ResetCause::None, false, healthy()) == BootPath::Run);
    CHECK(boot_path(ResetCause::Brownout, false, healthy()) == BootPath::Run);
    // VBUS with no wake bit: not a wake at all, whatever raised it.
    CHECK(boot_path(ResetCause::UsbVbus, false, healthy()) == BootPath::Run);
    CHECK(boot_path(ResetCause::PowerOn | ResetCause::UsbVbus, false, healthy()) == BootPath::Run);
}

TEST_CASE("wake: on a cell that can run, the reset pin and a fault get a device at once") {
    // A deliberate reset while the cable happens to be in.
    CHECK(boot_path(ResetCause::LowPowerWake | ResetCause::UsbVbus | ResetCause::Pin, false,
                    healthy()) == BootPath::Run);
    CHECK(boot_path(ResetCause::Pin, false, healthy()) == BootPath::Run);
    // And a fault is never answered by going back to sleep, whatever else is set.
    CHECK(boot_path(ResetCause::Watchdog | ResetCause::UsbVbus, false, healthy()) == BootPath::Run);
    CHECK(boot_path(ResetCause::Watchdog | ResetCause::LowPowerWake, false, healthy()) ==
          BootPath::Run);
}

// Two units woke in a bag, ran about 45 mA with nothing to switch them off, and were found flat.
TEST_CASE(
    "wake: a button wake becomes a device only by a hold, whatever the button reads at boot") {
    CHECK(boot_path(ResetCause::LowPowerWake, /*button_down=*/true, healthy()) ==
          BootPath::AwaitHold);
    CHECK(boot_path(ResetCause::LowPowerWake, /*button_down=*/false, healthy()) ==
          BootPath::AwaitHold);
}

TEST_CASE("wake: a press held until the wake hold becomes a device") {
    WakeHold hold;
    CHECK(hold.step(600, /*button_down=*/true) == BootPath::AwaitHold);
    CHECK(hold.step(kWakeHoldMs - 1, true) == BootPath::AwaitHold);
    CHECK(hold.step(kWakeHoldMs, true) == BootPath::Run);

    // A boot slow enough to reach the app past the hold needs no wait on top.
    WakeHold slow;
    CHECK(slow.step(kWakeHoldMs + 200, true) == BootPath::Run);
}

TEST_CASE("wake: a press released before the wake hold goes back to sleep") {
    WakeHold hold;
    CHECK(hold.step(600, true) == BootPath::AwaitHold);
    CHECK(hold.step(kWakeHoldMs - 50, false) == BootPath::AwaitHold);
    // Released before the hold and confirmed after it: the clock does not rescue it.
    CHECK(hold.step(kWakeHoldMs, false) == BootPath::AwaitHold);
    CHECK(hold.step(kWakeHoldMs - 50 + kReleaseSettleMs, false) == BootPath::SleepAgain);

    WakeHold bump;
    CHECK(bump.step(600, false) == BootPath::AwaitHold);
    CHECK(bump.step(600 + kReleaseSettleMs, false) == BootPath::SleepAgain);
}

// A boot that reaches the app late reads a released button after the deadline it never met.
TEST_CASE("wake: a button first seen up after the wake hold waits for the debounce, then sleeps") {
    WakeHold hold;
    CHECK(hold.step(kWakeHoldMs + 200, false) == BootPath::AwaitHold);
    CHECK(hold.step(kWakeHoldMs + 200 + kReleaseSettleMs - 1, false) == BootPath::AwaitHold);
    CHECK(hold.step(kWakeHoldMs + 200 + kReleaseSettleMs, false) == BootPath::SleepAgain);
}

// A probe on the SWD lines is a person at a bench, not a bag.
TEST_CASE("wake: a debugger that wakes the unit gets a device without the button") {
    CHECK(boot_path(ResetCause::Debug | ResetCause::LowPowerWake, false, healthy()) ==
          BootPath::Run);
    CHECK(boot_path(ResetCause::Debug, false, healthy()) == BootPath::Run);
    CHECK(boot_path(ResetCause::Debug | ResetCause::LowPowerWake, false,
                    healthy(kBootLockoutMv - 1)) == BootPath::SleepAgain);
}

// The wake pin is P1.10 with no debounce, and a thumb in a glove does not press evenly.
TEST_CASE("wake: a contact that chatters under a held thumb does not end the wake hold") {
    WakeHold hold;
    CHECK(hold.step(600, true) == BootPath::AwaitHold);
    CHECK(hold.step(650, false) == BootPath::AwaitHold);
    CHECK(hold.step(660, true) == BootPath::AwaitHold);
    CHECK(hold.step(kWakeHoldMs, true) == BootPath::Run);
}

// Left to boot, each press costs the panel and kParkSettleMs to reach the same cutoff.
TEST_CASE("wake: a flat cell refuses the boot, whoever asks and however they ask") {
    const BootCell flat = healthy(power::kFlatMv);
    CHECK(boot_path(ResetCause::LowPowerWake, /*button_down=*/true, flat) == BootPath::SleepAgain);
    CHECK(boot_path(ResetCause::Pin, false, flat) == BootPath::SleepAgain);
    CHECK(boot_path(ResetCause::PowerOn, false, flat) == BootPath::SleepAgain);
    CHECK(boot_path(ResetCause::Watchdog, false, healthy(power::kFlatMv - 1)) ==
          BootPath::SleepAgain);
    CHECK(boot_path(ResetCause::Brownout, false, flat) == BootPath::SleepAgain);

    CHECK(boot_path(ResetCause::PowerOn, false, healthy(kBootLockoutMv - 1)) ==
          BootPath::SleepAgain);
    CHECK(boot_path(ResetCause::PowerOn, false, healthy(kBootLockoutMv)) == BootPath::Run);
}

// A fault reset in flight met the switch-on lockout and left the pilot dark until a cable.
TEST_CASE("wake: a unit that reset itself runs on any cell the cutoff would have kept flying") {
    const BootCell low = healthy((power::kFlatMv + kBootLockoutMv) / 2);
    for (const ResetCause fault :
         {ResetCause::Watchdog, ResetCause::Lockup, ResetCause::Software}) {
        CAPTURE(static_cast<uint32_t>(fault));
        CHECK(boot_path(fault, false, low) == BootPath::Run);
        CHECK(boot_path(fault, false, healthy(power::kFlatMv)) == BootPath::Run);
        CHECK(boot_path(fault, false, healthy(power::kFlatMv - 1)) == BootPath::SleepAgain);
    }
    CHECK(boot_path(ResetCause::PowerOn, false, low) == BootPath::SleepAgain);
    CHECK(boot_path(ResetCause::LowPowerWake, true, low) == BootPath::SleepAgain);
    CHECK(boot_path(ResetCause::Pin, false, low) == BootPath::SleepAgain);
}

// A refusal with no way out is a brick, and this way out also fills the cell.
TEST_CASE("wake: a flat cell on the cable gets a device when a person asks for one") {
    BootCell on_charge = healthy(3000);
    on_charge.external_power = true;
    CHECK(boot_path(ResetCause::Pin, /*button_down=*/true, on_charge) == BootPath::Run);
    CHECK(boot_path(ResetCause::LowPowerWake | ResetCause::UsbVbus, false, on_charge) ==
          BootPath::SleepAgain);
}

// The lockout's own catastrophic failure: a unit bricked by a divider nobody read.
TEST_CASE("wake: a cell nobody read never refuses a boot") {
    CHECK(boot_path(ResetCause::PowerOn, false, BootCell{}) == BootPath::Run);
    CHECK(boot_path(ResetCause::PowerOn, false, BootCell{3000, /*valid=*/false, false}) ==
          BootPath::Run);
    CHECK(boot_path(ResetCause::PowerOn, false, healthy(200)) == BootPath::Run);
    CHECK(boot_path(ResetCause::PowerOn, false, healthy(kImplausibleFloorMv)) == BootPath::Run);
}

// A cell emptied on a shelf ran no shutdown, so the refusal is the only thing left to name it.
TEST_CASE("wake: the boot a flat cell refuses names it for the unit that never shut down") {
    CHECK(refused_frame(healthy(power::kFlatMv), CellOnGlass::None) == RefusedFrame::FlatCell);
    CHECK(refused_frame(healthy(kBootLockoutMv - 1), CellOnGlass::None) == RefusedFrame::FlatCell);

    // Pushing the frame it is already wearing is seconds of panel rail for no change.
    CHECK(refused_frame(healthy(power::kFlatMv), CellOnGlass::Flat) == RefusedFrame::Leave);
}

// A cell that went on draining after a switch-off asked for the cable has run past asking.
TEST_CASE("wake: a flat cell refused under the low word names itself instead") {
    CHECK(refused_frame(healthy(kBootLockoutMv - 1), CellOnGlass::Low) == RefusedFrame::FlatCell);
    CHECK(refused_frame(healthy(power::kFlatMv), CellOnGlass::Low) == RefusedFrame::FlatCell);
}

// A bootloader that let a charger wake through would get the frame of every armed device.
TEST_CASE("wake: a charger wake that reaches the refusal takes the word back off the glass") {
    BootCell on_charge = healthy(3000);
    on_charge.external_power = true;
    CHECK(refused_frame(on_charge, CellOnGlass::Flat) == RefusedFrame::Wordmark);
    CHECK(refused_frame(on_charge, CellOnGlass::Low) == RefusedFrame::Wordmark);
    CHECK(refused_frame(on_charge, CellOnGlass::None) == RefusedFrame::Leave);

    // A cell nobody read is not a flat one, here as everywhere else in this file.
    CHECK(refused_frame(BootCell{}, CellOnGlass::Flat) == RefusedFrame::Wordmark);
    CHECK(refused_frame(healthy(200), CellOnGlass::None) == RefusedFrame::Leave);

    CHECK(std::string(to_string(RefusedFrame::FlatCell)) == "FLAT CELL");
    CHECK(std::string(to_string(RefusedFrame::Wordmark)) == "WORDMARK");
    CHECK(std::string(to_string(RefusedFrame::Leave)) == "LEAVE");
}

TEST_CASE("wake: a charger wake is named without hiding a fault that came with it") {
    // The diagnosis a pilot needs is still the fault: the charger only names the
    // boot when nothing worse did.
    CHECK(classify(ResetCause::Watchdog | ResetCause::UsbVbus) == ResetReason::Watchdog);
    CHECK(classify(ResetCause::Pin | ResetCause::UsbVbus) == ResetReason::Pin);
    CHECK(std::string(to_string(BootPath::SleepAgain)) == "SLEEP AGAIN");
    CHECK(std::string(to_string(BootPath::Run)) == "RUN");
    CHECK(std::string(to_string(BootPath::AwaitHold)) == "AWAIT HOLD");
}
