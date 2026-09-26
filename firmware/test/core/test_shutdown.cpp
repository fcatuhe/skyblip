// The road from running to SYSTEM OFF: what starts a shutdown, and the order the rails go down in.
#include <initializer_list>
#include <string>

#include "core/power/shutdown.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::power;

// Three things ask for the rails to drop and all three take the same road. The
// order matters more than the entry: the radio and the panel are parked first,
// and the wake pin is armed only once the button is actually up.

TEST_CASE("shutdown: a short press is not a shutdown, a long one is") {
    ShutdownSequencer seq;
    uint32_t t = 0;
    seq.tick(t, false);  // the button starts up: the hold is armed

    for (t = 10; t < 500; t += 10) seq.tick(t, true);
    seq.tick(t, false);
    CHECK(seq.phase() == ShutdownPhase::Running);
    CHECK(seq.reason() == ShutdownReason::None);

    for (t = 1000; t < 1000 + kLongPressMs + 100; t += 10) seq.tick(t, true);
    CHECK(seq.phase() == ShutdownPhase::Parking);
    CHECK(seq.reason() == ShutdownReason::LongPress);
}

TEST_CASE("shutdown: the pad held through the press asks to be stowed, not switched off") {
    ShutdownSequencer seq;
    uint32_t t = 0;
    seq.tick(t, false);
    for (t = 10; t <= 10 + kLongPressMs; t += 10) seq.tick(t, true, /*pad_down=*/true);
    CHECK(seq.phase() == ShutdownPhase::Parking);
    CHECK(seq.reason() == ShutdownReason::Stow);
    CHECK(std::string(to_string(ShutdownReason::Stow)) == "STOW");
}

TEST_CASE("shutdown: a pad that was not held throughout is an ordinary power-off") {
    ShutdownSequencer late;
    uint32_t t = 0;
    late.tick(t, false);
    for (t = 10; t < 10 + kLongPressMs / 2; t += 10) late.tick(t, true, /*pad_down=*/true);
    for (; t <= 10 + kLongPressMs; t += 10) late.tick(t, true, /*pad_down=*/false);
    CHECK(late.reason() == ShutdownReason::LongPress);

    ShutdownSequencer brushed;
    t = 0;
    brushed.tick(t, false);
    for (t = 10; t < 10 + kLongPressMs / 2; t += 10) brushed.tick(t, true, /*pad_down=*/false);
    for (; t <= 10 + kLongPressMs; t += 10) brushed.tick(t, true, /*pad_down=*/true);
    CHECK(brushed.reason() == ShutdownReason::LongPress);
}

// A pad line that reads touched at rest would turn every power-off into a stow (#60).
TEST_CASE("shutdown: a pad never seen up since boot is not a stow") {
    ShutdownSequencer stuck;
    uint32_t t = 0;
    stuck.tick(t, false, /*pad_down=*/true);
    for (t = 10; t <= 10 + kLongPressMs; t += 10) stuck.tick(t, true, /*pad_down=*/true);
    CHECK(stuck.phase() == ShutdownPhase::Parking);
    CHECK(stuck.reason() == ShutdownReason::LongPress);

    ShutdownSequencer seen_up;
    seen_up.tick(0, false, /*pad_down=*/false);
    seen_up.tick(10, false, /*pad_down=*/true);
    for (t = 20; t <= 20 + kLongPressMs; t += 10) seen_up.tick(t, true, /*pad_down=*/true);
    CHECK(seen_up.reason() == ShutdownReason::Stow);
}

// A cheek, a raindrop or a bag rests on the pad: it may never switch anything off.
TEST_CASE("shutdown: the pad on its own switches nothing off") {
    ShutdownSequencer seq;
    seq.tick(0, false);
    for (uint32_t t = 10; t < 30000; t += 10) seq.tick(t, /*button_down=*/false, /*pad_down=*/true);
    CHECK(seq.phase() == ShutdownPhase::Running);
    CHECK(seq.reason() == ShutdownReason::None);
}

TEST_CASE("shutdown: a stow waits for the button like any other press") {
    ShutdownSequencer seq;
    seq.request(ShutdownReason::Stow, 0);
    CHECK(seq.waits_for_release());
    for (uint32_t t = 0; t <= kParkMs; t += 10) seq.tick(t, true, /*pad_down=*/true);
    CHECK(seq.phase() == ShutdownPhase::AwaitRelease);
    CHECK(button_wake_after(ShutdownReason::Stow, /*external_power=*/false) == ButtonWake::Armed);
}

TEST_CASE("shutdown: the wake pin waits for the button to come up") {
    ShutdownSequencer seq;
    uint32_t t = 0;
    seq.tick(t, false);
    for (t = 10; t <= 10 + kLongPressMs; t += 10) seq.tick(t, true);
    REQUIRE(seq.phase() == ShutdownPhase::Parking);

    // The panel needs its full refresh before the rails may go.
    for (; t < 10 + kLongPressMs + kParkMs; t += 10) seq.tick(t, true);
    CHECK(seq.phase() == ShutdownPhase::Parking);

    // Parked, but the finger is still on the button: a level-sensed wake pin
    // armed now brings the device straight back up.
    for (; t < 30000; t += 10) seq.tick(t, true);
    CHECK(seq.phase() == ShutdownPhase::AwaitRelease);
    CHECK_FALSE(seq.ready_to_power_off());

    const uint32_t released = t;
    for (; t < released + kReleaseSettleMs; t += 10) seq.tick(t, false);
    CHECK_FALSE(seq.ready_to_power_off());
    seq.tick(t + kReleaseSettleMs, false);
    CHECK(seq.phase() == ShutdownPhase::Off);
    CHECK(seq.ready_to_power_off());
}

// A bag on the button must not hold the rails up on the cell that asked to go.
TEST_CASE("shutdown: a low-battery shutdown does not wait for a button it will not arm") {
    ShutdownSequencer seq;
    seq.request(ShutdownReason::LowBattery, 0);
    uint32_t t = 0;
    for (; t < kParkMs; t += 10) seq.tick(t, /*button_down=*/true);
    CHECK(seq.phase() == ShutdownPhase::Parking);

    seq.tick(kParkMs, /*button_down=*/true);
    CHECK(seq.phase() == ShutdownPhase::Off);
    CHECK(seq.ready_to_power_off());

    // A long press still waits: that one does come back on a press.
    ShutdownSequencer pressed;
    pressed.request(ShutdownReason::LongPress, 0);
    for (t = 0; t <= kParkMs; t += 10) pressed.tick(t, true);
    CHECK(pressed.phase() == ShutdownPhase::AwaitRelease);
}

// The swap takes the same road out as a power-off: the panel gets its full refresh
// before the bootloader is handed the device, and the reason is spelled for the log.
TEST_CASE("shutdown: an install parks the panel like a power-off and is named as one") {
    ShutdownSequencer seq;
    seq.request(ShutdownReason::Install, 0);
    CHECK(std::string(to_string(ShutdownReason::Install)) == "INSTALL");
    CHECK(seq.going_down());
    uint32_t t = 0;
    for (; t < kParkMs; t += 10) seq.tick(t, /*button_down=*/false);
    CHECK(seq.phase() == ShutdownPhase::Parking);
    for (; t < kParkMs + kReleaseSettleMs + 50; t += 10) seq.tick(t, false);
    CHECK(seq.phase() == ShutdownPhase::Off);
    CHECK(seq.ready_to_power_off());
}

TEST_CASE("shutdown: a device woken by the button does not switch itself off again") {
    // SYSTEM OFF is left by a press, so the first thing the sequencer ever sees
    // is a button that is already down. Counting that as a hold powers the
    // device off before the panel has drawn a single frame.
    ShutdownSequencer seq;
    for (uint32_t t = 0; t < 10000; t += 10) seq.tick(t, true);
    CHECK(seq.phase() == ShutdownPhase::Running);

    // Once it has been released, the next hold counts.
    uint32_t t = 10000;
    seq.tick(t, false);
    for (; t <= 10000 + kLongPressMs + 10; t += 10) seq.tick(t, true);
    CHECK(seq.phase() == ShutdownPhase::Parking);
}

TEST_CASE("shutdown: low battery and the link take the same road as the button") {
    for (const ShutdownReason reason : {ShutdownReason::LowBattery, ShutdownReason::LinkRequest}) {
        ShutdownSequencer seq;
        seq.request(reason, 0);
        CHECK(seq.phase() == ShutdownPhase::Parking);
        CHECK(seq.reason() == reason);
        CHECK(seq.going_down());

        uint32_t t = 0;
        for (; t < kParkMs + kReleaseSettleMs + 100; t += 10) seq.tick(t, false);
        CHECK(seq.ready_to_power_off());
    }
}

TEST_CASE("shutdown: nothing cancels a shutdown once it has started") {
    ShutdownSequencer seq;
    seq.request(ShutdownReason::LowBattery, 0);
    seq.request(ShutdownReason::LinkRequest, 10);
    CHECK(seq.reason() == ShutdownReason::LowBattery);

    for (uint32_t t = 0; t < 20000; t += 10) seq.tick(t, false);
    CHECK(seq.phase() == ShutdownPhase::Off);
    // Even a fresh press cannot bring it back.
    seq.tick(20000, true);
    CHECK(seq.phase() == ShutdownPhase::Off);
}

TEST_CASE("shutdown: the hold is readable while it fills up") {
    ShutdownSequencer seq;
    seq.tick(0, false);
    CHECK(seq.held_ms(0) == 0);
    seq.tick(100, true);
    seq.tick(600, true);
    CHECK(seq.held_ms(600) == 500);
    seq.tick(700, false);
    CHECK(seq.held_ms(700) == 0);
}

// M. The whole sequencer across the 49.7-day wrap of ports::Clock::millis(): the
// long press, the park, and the settle after the button comes up. It is written as
// unsigned differences from a stamp guarded by a flag, and this is the case that
// keeps it that way. What the two failures would be: a press that never reaches
// two seconds, so the device cannot be switched off at all until the counter comes
// round; or a park that expires the instant it starts, so the rails go while the
// panel is still refreshing.
TEST_CASE("shutdown: the press, the park and the settle span the 49.7-day wrap") {
    ShutdownSequencer seq;
    const uint32_t before = 0xFFFFFF00u;  // 256 ms short of the wrap
    uint32_t t = before;
    seq.tick(t, false);  // the button starts up: the hold is armed

    // Pressed 256 ms before the wrap, held through it. The hold reads as elapsed
    // time on both sides of zero.
    seq.tick(t, true);
    CHECK(seq.held_ms(before + 500u) == 500);
    for (t = before; t != before + kLongPressMs - 10u; t += 10) seq.tick(t, true);
    CHECK(seq.phase() == ShutdownPhase::Running);
    seq.tick(before + kLongPressMs, true);
    CHECK(seq.phase() == ShutdownPhase::Parking);
    CHECK(seq.reason() == ShutdownReason::LongPress);

    // The park is three seconds of panel time, not zero and not seven weeks.
    const uint32_t parking_since = before + kLongPressMs;
    seq.tick(parking_since + kParkMs - 1u, true);
    CHECK(seq.phase() == ShutdownPhase::Parking);
    seq.tick(parking_since + kParkMs, true);
    CHECK(seq.phase() == ShutdownPhase::AwaitRelease);

    // The finger comes up, and the settle before the wake pin is armed is
    // measured the same way.
    const uint32_t released = parking_since + kParkMs + 40u;
    seq.tick(released, false);
    seq.tick(released + kReleaseSettleMs - 1u, false);
    CHECK_FALSE(seq.ready_to_power_off());
    seq.tick(released + kReleaseSettleMs, false);
    CHECK(seq.phase() == ShutdownPhase::Off);
    CHECK(seq.ready_to_power_off());
}

// The order the rails are allowed to collapse in. Nothing here is about WHEN
// the device switches off - the sequencer above owns that - and everything is
// about what is still powered when each command is sent. Three of these steps
// are silently useless one position later.

namespace {

class RecordingSink : public power::PowerDownSink {
   public:
    void perform(power::PowerDownStep step) override {
        if (count < power::kPowerDownStepCount) steps[count++] = step;
    }
    int at(power::PowerDownStep step) const {
        for (int i = 0; i < count; i++)
            if (steps[i] == step) return i;
        return -1;
    }
    power::PowerDownStep steps[power::kPowerDownStepCount]{};
    int count{0};
};

}  // namespace

TEST_CASE("power down: every step runs once, in the order the table declares") {
    RecordingSink sink;
    power_down(sink, ButtonWake::Armed);
    REQUIRE(sink.count == kPowerDownStepCount);
    for (int i = 0; i < kPowerDownStepCount; i++) {
        CHECK(sink.steps[i] == kPowerDownOrder[i]);
        CHECK(sink.at(kPowerDownOrder[i]) == i);
        CHECK(to_string(kPowerDownOrder[i])[0] != '\0');
    }
}

// The bug this whole sequence exists to prevent: 0xB9 arrives at a part that has
// already lost its supply, the command does nothing, and an external flash draws
// its full standby current for the whole night the device spends in a flight bag
// looking switched off.
TEST_CASE("power down: the external flash is told to sleep while it still has a rail") {
    RecordingSink sink;
    power_down(sink, ButtonWake::Armed);
    CHECK(sink.at(PowerDownStep::ExternalFlashDeepPowerDown) <
          sink.at(PowerDownStep::PeripheralRailOff));
    // And over the lines the command travels on, which is why they are released
    // after it and not with the rest of the pins.
    CHECK(sink.at(PowerDownStep::ExternalFlashDeepPowerDown) <
          sink.at(PowerDownStep::ExternalFlashLinesReleased));
}

// A line left high on a panel with no rail back-feeds it, and the parked image drifts dark.
TEST_CASE("power down: the panel's lines are released before its rail, not after") {
    RecordingSink sink;
    power_down(sink, ButtonWake::Armed);
    CHECK(sink.at(PowerDownStep::PanelLinesReleased) < sink.at(PowerDownStep::PeripheralRailOff));
    CHECK(sink.at(PowerDownStep::PanelLinesReleased) < sink.at(PowerDownStep::DrivenPinsReleased));
}

TEST_CASE("power down: the radio is asleep before anything touches its reset line") {
    RecordingSink sink;
    power_down(sink, ButtonWake::Armed);
    CHECK(sink.at(PowerDownStep::RadioSleep) < sink.at(PowerDownStep::RadioResetAsserted));
    CHECK(sink.at(PowerDownStep::RadioSleep) < sink.at(PowerDownStep::DrivenPinsReleased));
}

TEST_CASE("power down: the GNSS is switched off, and before its supply goes") {
    RecordingSink sink;
    power_down(sink, ButtonWake::Armed);
    // The receiver with a fix is tens of milliamps: the one part that decides
    // whether an 850 mAh pack survives a night.
    REQUIRE(sink.at(PowerDownStep::GnssBackupOff) >= 0);
    CHECK(sink.at(PowerDownStep::GnssBackupOff) < sink.at(PowerDownStep::GnssResetAsserted));
    CHECK(sink.at(PowerDownStep::GnssResetAsserted) < sink.at(PowerDownStep::PeripheralRailOff));
}

TEST_CASE("power down: the rails go last, and the pins are released after them") {
    RecordingSink sink;
    power_down(sink, ButtonWake::Armed);
    const int rail = sink.at(PowerDownStep::PeripheralRailOff);
    const int aux = sink.at(PowerDownStep::AuxRailOff);
    for (const PowerDownStep step :
         {PowerDownStep::RadioSleep, PowerDownStep::ExternalFlashDeepPowerDown,
          PowerDownStep::ExternalFlashLinesReleased, PowerDownStep::PanelLinesReleased,
          PowerDownStep::GnssBackupOff, PowerDownStep::GnssResetAsserted,
          PowerDownStep::RadioResetAsserted}) {
        CHECK(sink.at(step) < rail);
        CHECK(sink.at(step) < aux);
    }
    // A pin driven high into a part with no supply powers it through its own
    // protection diode, so nothing is released until both rails are down.
    CHECK(rail < sink.at(PowerDownStep::DrivenPinsReleased));
    CHECK(aux < sink.at(PowerDownStep::DrivenPinsReleased));
    // And the wake pin is level-sensed: armed before the rails, it wakes the
    // device the instant they drop.
    CHECK(sink.at(PowerDownStep::WakePinArmed) == kPowerDownStepCount - 1);
}

// A press on a cell this flat is the ratchet, so only a cable brings it back.
TEST_CASE("power down: a low-battery shutdown drops the rails without arming the button") {
    CHECK(button_wake_after(ShutdownReason::LowBattery, false) == ButtonWake::Withheld);
    CHECK(button_wake_after(ShutdownReason::LongPress, false) == ButtonWake::Armed);
    CHECK(button_wake_after(ShutdownReason::LinkRequest, false) == ButtonWake::Armed);
    CHECK(button_wake_after(ShutdownReason::None, false) == ButtonWake::Armed);
    // A cable that arrived during the park: VBUS is already high, so there is no
    // rising edge left to wake on and the button is the only way back.
    CHECK(button_wake_after(ShutdownReason::LowBattery, /*external_power=*/true) ==
          ButtonWake::Armed);

    RecordingSink sink;
    power_down(sink, ButtonWake::Withheld);
    CHECK(sink.count == kPowerDownStepCount - 1);
    CHECK(sink.at(PowerDownStep::WakePinArmed) == -1);
    for (int i = 0; i < kPowerDownStepCount - 1; i++) CHECK(sink.steps[i] == kPowerDownOrder[i]);
}
