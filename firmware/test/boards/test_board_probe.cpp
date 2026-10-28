// What the board asks its own hardware at bring-up, and what it concludes.
//
// The T-Echo Plus is not one board: LilyGO fits either of two flash parts,
// either of two barometer addresses, one of at least five e-paper lots, and -
// this is the one that bit us - a DRV2605 haptic driver where pins.h claimed a
// vibration motor on a pin. Every case here is the board being handed a
// different unit off the same production line.
#include <string>

#include "boards/lilygo/t_echo_plus/board.h"
#include "core/bus/bus.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/platform.h"

using namespace skyblip;

namespace {

using Board = boards::TEchoPlus<platform::host::Platform>;
using boards::t_echo_plus::kRtcAddress;

}  // namespace

TEST_CASE("board: one scan names everything on the sensor bus, driven or not") {
    platform::host::Platform platform;
    bus::Bus bus;
    Board board{platform, bus};

    const ports::Inventory& inventory = board.inventory();

    // The barometer, at the address that actually answered rather than at
    // whichever of the two the devicetree happened to declare first.
    CHECK(int(inventory.barometer_address) == 0x76);
    CHECK(inventory.has_i2c_address(0x76));
    CHECK_FALSE(inventory.has_i2c_address(0x77));

    // The haptic driver, which is the whole reason the scan exists.
    CHECK(inventory.has_i2c_address(parts::Drv2605::kAddress));

    // And the two parts nothing drives, because a scan that hid the soldered-on
    // parts would be a scan nobody could use: this is the entire evidence for the
    // RTC that project/2-DEVICES.md records as fitted.
    CHECK(inventory.has_i2c_address(kRtcAddress));
    CHECK(inventory.has_i2c_address(parts::Bhi260::kAddress));

    // Ascending, so the page reads the same way twice.
    for (uint8_t i = 1; i < inventory.i2c_count; i++)
        CHECK(inventory.i2c_addresses[i - 1] < inventory.i2c_addresses[i]);
}

TEST_CASE("board: a haptic that answers the bus is a capability, and it is the part") {
    platform::host::Platform platform;
    bus::Bus bus;
    Board board{platform, bus};

    CHECK(ports::has(board.capabilities(), ports::Capability::Haptic));
    CHECK(board.inventory().haptic == ports::HapticKind::WaveformDriver);
    CHECK(board.haptic().ready());

    // The pulse reaches the chip's registers, not a pin: the annunciator role the
    // alarm service holds is wired to the part the board found.
    ports::Roles roles = board.roles();
    models::Drv2605& chip = platform.chips().haptic;
    CHECK_FALSE(chip.moving());

    roles.annunciator.vibrate();
    CHECK(chip.moving());
    roles.annunciator.still();
    CHECK_FALSE(chip.moving());
}

// A loop that stalls mid-call would otherwise hold an output on until the watchdog bites.
TEST_CASE("board: an output left on past its ceiling is cut, the motor and the buzzer both") {
    platform::host::Platform platform;
    bus::Bus bus;
    Board board{platform, bus};
    ports::Roles roles = board.roles();
    models::Drv2605& chip = platform.chips().haptic;
    bus::State state{};

    roles.annunciator.vibrate();
    board.poll(state, 100);
    board.poll(state, 100 + annunciation::kAnnunciatorOnCeilingMs - 10);
    CHECK(chip.moving());
    board.poll(state, 100 + annunciation::kAnnunciatorOnCeilingMs);
    CHECK_FALSE(chip.moving());

    roles.annunciator.tone(annunciation::kNoteA7Hz, ports::Loudness::Loud);
    board.poll(state, 3000);
    board.poll(state, 3000 + annunciation::kAnnunciatorOnCeilingMs - 10);
    CHECK(platform.annunciator().sounding());
    board.poll(state, 3000 + annunciation::kAnnunciatorOnCeilingMs);
    CHECK_FALSE(platform.annunciator().sounding());
    CHECK(platform.annunciator().guard_cuts() == 2);
}

TEST_CASE("board: the guard counts from turn-on: an off re-arms it, a pitch change does not") {
    platform::host::Platform platform;
    bus::Bus bus;
    Board board{platform, bus};
    ports::Roles roles = board.roles();
    models::Drv2605& chip = platform.chips().haptic;
    bus::State state{};
    constexpr uint32_t kCeilingMs = annunciation::kAnnunciatorOnCeilingMs;

    roles.annunciator.vibrate();
    board.poll(state, 100);
    roles.annunciator.still();
    board.poll(state, 100 + kCeilingMs + 10);
    CHECK(platform.annunciator().guard_cuts() == 0);

    roles.annunciator.vibrate();
    board.poll(state, 2500);
    roles.annunciator.still();
    roles.annunciator.vibrate();
    board.poll(state, 4000);
    board.poll(state, 2500 + kCeilingMs + 10);
    CHECK(chip.moving());
    CHECK(platform.annunciator().guard_cuts() == 0);
    roles.annunciator.still();

    roles.annunciator.tone(annunciation::kNoteA7Hz, ports::Loudness::Loud);
    board.poll(state, 8000);
    roles.annunciator.tone(annunciation::kNoteCSharp8Hz, ports::Loudness::Loud);
    board.poll(state, 8000 + kCeilingMs - 10);
    CHECK(platform.annunciator().sounding());
    board.poll(state, 8000 + kCeilingMs);
    CHECK_FALSE(platform.annunciator().sounding());
    CHECK(platform.annunciator().guard_cuts() == 1);
}

TEST_CASE("board: a Plus with no haptic driver does not claim a vibration motor") {
    platform::host::Platform platform;
    // The unit is fitted for a haptic and the part does not answer: an empty pad,
    // a dead chip, a board that turned out to be a plain T-Echo.
    platform.chips().haptic.answers = false;
    bus::Bus bus;
    Board board{platform, bus};

    CHECK_FALSE(ports::has(board.capabilities(), ports::Capability::Haptic));
    CHECK(board.inventory().haptic == ports::HapticKind::None);
    CHECK_FALSE(board.inventory().has_i2c_address(parts::Drv2605::kAddress));

    // The call site does not change and does not check: it asks for a pulse and
    // gets whatever this board can make, which here is nothing at all.
    ports::Roles roles = board.roles();
    roles.annunciator.vibrate();
    CHECK_FALSE(platform.chips().haptic.moving());
}

TEST_CASE("board: the panel it identified is on the inventory by name") {
    platform::host::Platform platform;
    platform.chips().epd.signature = parts::panels::kGdeh0154D67Syx2118;
    bus::Bus bus;
    Board board{platform, bus};

    CHECK(board.display().panel() == parts::Panel::Gdeh0154D67Syx2118);
    CHECK(std::string(board.inventory().panel_name) == "D67/2118");
}

TEST_CASE("board: a panel nobody has fingerprinted is named, not guessed at") {
    platform::host::Platform platform;
    bus::Bus bus;
    Board board{platform, bus};

    // The default: no fingerprint taken, which is what this board will report on
    // silicon until a Plus is read on a bench.
    CHECK(board.display().panel() == parts::Panel::Unknown);
    CHECK(std::string(board.inventory().panel_name) == "NO ID");
    // And the display is still a capability. An identification that could refuse
    // to drive the glass would blank the one page that explains a dead device.
    CHECK(ports::has(board.capabilities(), ports::Capability::Display));
}

// Read off our own two units the day the page had them the wrong way round.
TEST_CASE("board: a buzzer pin held down by its drive stage is a fitted buzzer") {
    platform::host::Platform platform;
    platform.set_buzzer_pin_held_low(true);
    bus::Bus bus;
    Board board{platform, bus};

    CHECK(ports::has(board.capabilities(), ports::Capability::Buzzer));
}

TEST_CASE("board: a buzzer pin that follows the pull-up is an empty pad, and the haptic stays") {
    platform::host::Platform platform;
    platform.set_buzzer_pin_held_low(false);
    bus::Bus bus;
    Board board{platform, bus};

    CHECK_FALSE(ports::has(board.capabilities(), ports::Capability::Buzzer));
    // The haptic is a different part on a different bus and is unaffected...
    CHECK(ports::has(board.capabilities(), ports::Capability::Haptic));

    // ...and still reachable: the annunciator is one role over two parts.
    ports::Roles roles = board.roles();
    roles.annunciator.vibrate();
    CHECK(platform.chips().haptic.moving());
}

TEST_CASE("board: a barometer at LilyGO's address is the one reported") {
    platform::host::Platform platform;
    bus::Bus bus;
    // Our BOM says 0x76 and LilyGO's own README says 0x77. This is the unit that
    // came the other way.
    platform.i2c_bus().answer(0x76, false);
    platform.i2c_bus().answer(0x77, true);
    Board board{platform, bus};

    CHECK(int(board.inventory().barometer_address) == 0x77);
}

TEST_CASE("board: the status lamp is a devicetree fact, and a board without one says so") {
    // The one part on this board that cannot be probed: an LED answers nothing.
    // So presence is the devicetree node existing and its GPIO port being ready,
    // which is the platform's question, and the board only carries the verdict
    // through. Asserted here because a board that dropped the bit would produce a
    // device with a lamp it never lights and no way to tell.
    platform::host::Platform fitted;
    bus::Bus bus;
    Board with_a_lamp{fitted, bus};
    CHECK(ports::has(with_a_lamp.capabilities(), ports::Capability::Indicator));

    constexpr ports::Capabilities kNoLamp = static_cast<ports::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(ports::Capability::Indicator));
    platform::host::Platform bare{kNoLamp};
    bus::Bus bare_bus;
    Board without{bare, bare_bus};
    CHECK_FALSE(ports::has(without.capabilities(), ports::Capability::Indicator));
    // And it is still a flyable board: the lamp is not required for anything.
    CHECK(ports::has(without.capabilities(), ports::Capability::Rf | ports::Capability::Gnss));
}
