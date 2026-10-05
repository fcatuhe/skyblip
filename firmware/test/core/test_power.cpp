// One curve, read off the cable only: on it the divider reads the USB rail and
// not the cell. These pin down that the gauge never walks the wrong way, ignores
// the sag of a 14 dBm burst, and shows no cell it cannot see. A percentage that
// jumps when the radio keys is a gauge a pilot stops believing.
#include <initializer_list>
#include <vector>

#include "core/events/sensor.h"
#include "core/power/battery.h"
#include "core/power/cutoff.h"
#include "core/power/wake.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::power;

static events::BatterySample sample(uint16_t millivolts, bool external_power = false) {
    return events::BatterySample{millivolts, external_power};
}

// The gauge medians three readings, so a settled value takes three - which is the
// same reason a single transmit burst cannot move the needle.
static void settle(Gauge& gauge, uint16_t millivolts, bool external_power = false) {
    for (int i = 0; i < 4; i++) gauge.apply(sample(millivolts, external_power));
}

TEST_CASE("battery: the curve is monotonic and clamps at both ends") {
    CHECK(percent_from_mv(3000) == 0);
    CHECK(percent_from_mv(kEmptyMv) == 0);
    CHECK(percent_from_mv(kFullMv) == 100);
    CHECK(percent_from_mv(4500) == 100);

    uint8_t previous = 0;
    for (uint16_t mv = kEmptyMv; mv <= kFullMv; mv += 10) {
        const uint8_t percent = percent_from_mv(mv);
        CHECK(percent >= previous);
        previous = percent;
    }
}

TEST_CASE("battery: the flat middle of the cell is not a straight line") {
    // 3.70 -> 3.80 V is a quarter of the capacity; 4.10 -> 4.20 V is a twentieth.
    const int mid = percent_from_mv(3800) - percent_from_mv(3700);
    const int top = percent_from_mv(4200) - percent_from_mv(4100);
    CHECK(mid > top);
}

// The window the gauge believes: a floating divider under it, a rail over it.
TEST_CASE("battery: a reading is the cell only between the floor and the ceiling") {
    CHECK_FALSE(plausible_mv(0));
    CHECK_FALSE(plausible_mv(kImplausibleFloorMv));
    CHECK(plausible_mv(kImplausibleFloorMv + 1));
    CHECK(plausible_mv(kFullMv));
    CHECK(plausible_mv(kImplausibleCeilingMv));
    CHECK_FALSE(plausible_mv(kImplausibleCeilingMv + 1));
}

TEST_CASE("gauge: nothing is reported before the first reading") {
    Gauge gauge;
    CHECK_FALSE(gauge.state().valid);
    CHECK(gauge.state().millivolts == 0);

    gauge.apply(sample(3900));
    CHECK(gauge.state().valid);
    // The first reading stands on its own rather than being mixed with zeroes.
    CHECK(gauge.state().millivolts == 3900);
    CHECK(gauge.state().percent == percent_from_mv(3900));
    CHECK_FALSE(gauge.state().charging);
}

TEST_CASE("gauge: one transmit burst does not move the gauge") {
    Gauge gauge;
    settle(gauge, 3900);
    const uint8_t before = gauge.state().percent;

    // A 14 dBm burst sags the rail for a single reading. The median throws it out
    // whole: 200 mV of transient must not read as a fifth of the pack gone.
    gauge.apply(sample(3700));
    CHECK(gauge.state().millivolts == 3900);
    CHECK(gauge.state().percent == before);

    // Two readings in a row are the cell, not a burst.
    gauge.apply(sample(3700));
    CHECK(gauge.state().millivolts == 3700);
    CHECK(gauge.state().percent < before);
}

TEST_CASE("gauge: the reading the median threw out is still the reading the bench sees") {
    Gauge gauge;
    settle(gauge, 3900);
    gauge.apply(sample(3700));
    CHECK(gauge.state().millivolts == 3900);
    CHECK(gauge.state().sample_mv == 3700);
}

TEST_CASE("gauge: off the cable the percentage only falls") {
    Gauge gauge;
    settle(gauge, 3800);
    const uint8_t discharging = gauge.state().percent;

    // Noise upwards while running on the cell is noise, not charge: two readings
    // to get past the median, and the percentage still does not climb.
    gauge.apply(sample(3830));
    gauge.apply(sample(3830));
    CHECK(gauge.state().millivolts == 3830);
    CHECK(gauge.state().percent == discharging);

    // Draining further does move it.
    settle(gauge, 3700);
    CHECK(gauge.state().percent < discharging);
}

// E68BD9 read 4670 to 4700 mV for 3.5 h of charging from 45 %, and called it 100 % (#114).
TEST_CASE("gauge: on the cable the divider reads the rail, so there is no cell to show") {
    Gauge gauge;
    settle(gauge, 3750);
    REQUIRE(gauge.state().valid);

    settle(gauge, 4690, /*external_power=*/true);
    CHECK_FALSE(gauge.state().valid);
    CHECK(gauge.state().charging);
    CHECK(gauge.state().external_power);
    CHECK(gauge.state().percent == 0);
    CHECK(gauge.state().millivolts == 4690);
    CHECK(gauge.refused() == 0);
}

// Charged on the cable, the cell is where the first reading off it says, not under the old median.
TEST_CASE("gauge: unplugging starts the median over on the charged cell") {
    Gauge gauge;
    settle(gauge, 3750);
    settle(gauge, 4690, /*external_power=*/true);

    gauge.apply(sample(4150));
    CHECK(gauge.state().valid);
    CHECK_FALSE(gauge.state().charging);
    CHECK(gauge.state().millivolts == 4150);
    CHECK(gauge.state().percent == percent_from_mv(4150));
}

// 0B1B2C's rail sat above the ceiling for 7 h, and nothing said which bound it crossed (#114).
TEST_CASE("gauge: a refused reading is counted, and its value is what the gauge shows") {
    Gauge gauge;
    settle(gauge, 3900);

    gauge.apply(sample(4812, /*external_power=*/true));
    CHECK(gauge.refused() == 1);
    CHECK_FALSE(gauge.state().valid);
    CHECK(gauge.state().millivolts == 4812);
    CHECK(gauge.state().sample_mv == 4812);

    gauge.apply(sample(312));
    CHECK(gauge.refused() == 2);
    CHECK_FALSE(gauge.state().valid);
    CHECK(gauge.state().millivolts == 312);
    CHECK_FALSE(gauge.state().external_power);

    gauge.apply(sample(3880));
    CHECK(gauge.state().valid);
    CHECK(gauge.state().millivolts == 3880);
    CHECK(gauge.refused() == 2);
}

// The acting half. The gauge above says what the cell holds; this says when that
// number stops being a reading and becomes a decision. SoftRF's shape, because
// it is the one that survived contact with the same pack on the same board:
// 3.5 V warns, 3.2 V cuts off, three consecutive samples before either, and a
// 1.8 V floor under both so an unconnected divider cannot switch the device off
// in someone's hand.

TEST_CASE("cutoff: two samples below the cutoff are not enough, the third acts") {
    // Under the rule, two samples claim nothing at all: the monitor has not yet
    // separated a real cell from a burst sagging the rail.
    CutoffMonitor monitor;
    CHECK(monitor.apply(sample(3100)) == PowerLevel::Unknown);
    CHECK(monitor.apply(sample(3100)) == PowerLevel::Unknown);
    CHECK_FALSE(monitor.cutoff());
    CHECK(monitor.apply(sample(3100)) == PowerLevel::Flat);
    CHECK(monitor.cutoff());
}

TEST_CASE("cutoff: one good sample resets the count, so a transmit sag cannot act") {
    CutoffMonitor monitor;
    monitor.apply(sample(3100));
    monitor.apply(sample(3100));
    monitor.apply(sample(3800));  // the burst ended, the rail came back
    CHECK(monitor.level() == PowerLevel::Normal);

    monitor.apply(sample(3100));
    monitor.apply(sample(3100));
    CHECK_FALSE(monitor.cutoff());
}

static PowerLevel settled_at(uint16_t millivolts) {
    CutoffMonitor monitor;
    for (int i = 0; i < 4; i++) monitor.apply(sample(millivolts));
    return monitor.level();
}

// Exactly on a step is the step above it, one millivolt under is the step itself.
TEST_CASE("cutoff: every step is a boundary, not a range") {
    CHECK(settled_at(kLowMv) == PowerLevel::Normal);
    CHECK(settled_at(kLowMv - 1) == PowerLevel::Low);
    CHECK(settled_at(kCriticalMv) == PowerLevel::Low);
    CHECK(settled_at(kCriticalMv - 1) == PowerLevel::Critical);
    CHECK(settled_at(kFlatMv) == PowerLevel::Critical);
    CHECK(settled_at(kFlatMv - 1) == PowerLevel::Flat);
}

TEST_CASE("cutoff: critical comes before flat, never instead of it") {
    CutoffMonitor monitor;
    for (int i = 0; i < 3; i++) monitor.apply(sample(3400));
    CHECK(monitor.level() == PowerLevel::Critical);
    CHECK_FALSE(monitor.cutoff());

    for (int i = 0; i < 3; i++) monitor.apply(sample(3100));
    CHECK(monitor.cutoff());
}

// The knee a pilot plans on: the device says it everywhere, and still refuses nothing.
TEST_CASE("cutoff: low comes before critical, three samples deep, and refuses nothing") {
    CutoffMonitor monitor;
    for (int i = 0; i < 4; i++) monitor.apply(sample(3900));
    for (int i = 0; i < 2; i++) monitor.apply(sample(kLowMv - 1));
    CHECK(monitor.level() == PowerLevel::Normal);

    monitor.apply(sample(kLowMv - 1));
    CHECK(monitor.level() == PowerLevel::Low);
    CHECK(needs_charge(monitor.level()));
    CHECK(monitor.may_write(DurableWrite::Settings));
    CHECK(monitor.may_refresh(PanelRefresh::Routine));
}

TEST_CASE("cutoff: only low, critical and flat ask for the charger") {
    CHECK_FALSE(needs_charge(PowerLevel::Unknown));
    CHECK_FALSE(needs_charge(PowerLevel::Normal));
    CHECK(needs_charge(PowerLevel::Low));
    CHECK(needs_charge(PowerLevel::Critical));
    CHECK(needs_charge(PowerLevel::Flat));
}

// The cell sits on a step for the minutes it takes to cross it, and noise straddles the line.
TEST_CASE("cutoff: a level comes back up on three samples clear of its step by the margin") {
    CutoffMonitor monitor;
    for (int i = 0; i < 3; i++) monitor.apply(sample(kCriticalMv - 10));
    REQUIRE(monitor.level() == PowerLevel::Critical);

    for (int i = 0; i < 10; i++) monitor.apply(sample(kCriticalMv + kRecoveryMarginMv - 1));
    CHECK(monitor.level() == PowerLevel::Critical);

    monitor.apply(sample(kCriticalMv + kRecoveryMarginMv));
    monitor.apply(sample(kCriticalMv + kRecoveryMarginMv));
    CHECK(monitor.level() == PowerLevel::Critical);
    monitor.apply(sample(kCriticalMv - 10));
    monitor.apply(sample(kCriticalMv + kRecoveryMarginMv));
    CHECK(monitor.level() == PowerLevel::Critical);

    for (int i = 0; i < 2; i++) monitor.apply(sample(kCriticalMv + kRecoveryMarginMv));
    CHECK(monitor.level() == PowerLevel::Low);
}

// A falling cell is not slowed: the margin is only on the way up.
TEST_CASE("cutoff: a level goes down on three samples under its step, with no margin") {
    CutoffMonitor monitor;
    for (int i = 0; i < 3; i++) monitor.apply(sample(kCriticalMv - 10));
    for (int i = 0; i < 3; i++) monitor.apply(sample(kCriticalMv + kRecoveryMarginMv));
    REQUIRE(monitor.level() == PowerLevel::Low);

    for (int i = 0; i < 3; i++) monitor.apply(sample(kCriticalMv - 1));
    CHECK(monitor.level() == PowerLevel::Critical);
}

TEST_CASE("cutoff: a cell that recovers far climbs every step it cleared at once") {
    CutoffMonitor monitor;
    for (int i = 0; i < 3; i++) monitor.apply(sample(3400));
    REQUIRE(monitor.level() == PowerLevel::Critical);

    for (int i = 0; i < 3; i++) monitor.apply(sample(3900));
    CHECK(monitor.level() == PowerLevel::Normal);
}

// A run counted in a byte wrapped to zero after 255 samples and read as a cell back up.
TEST_CASE("cutoff: a cell resting on a step for many minutes stays on it") {
    CutoffMonitor monitor;
    for (int i = 0; i < 1000; i++) {
        monitor.apply(sample(kCriticalMv - 10));
        if (i >= kLevelSamples - 1) REQUIRE(monitor.level() == PowerLevel::Critical);
    }
}

// E68BD9's run to cutoff, its cell_mv every 30 s on 5 Oct, 02:39-02:45 and 03:39-03:43 UTC.
TEST_CASE("cutoff: a parked cell drifting across a step changes level once") {
    constexpr uint16_t kRunMv[] = {3607, 3598, 3591, 3600, 3600, 3600, 3600, 3594,
                                   3600, 3591, 3601, 3596, 3598, 3513, 3492, 3517,
                                   3501, 3496, 3492, 3505, 3487, 3498, 3492};
    CutoffMonitor monitor;
    PowerLevel previous = monitor.apply(sample(kRunMv[0]));
    REQUIRE(previous == PowerLevel::Normal);

    std::vector<PowerLevel> changes;
    for (const uint16_t millivolts : kRunMv) {
        for (int i = 0; i < kLevelSamples; i++) monitor.apply(sample(millivolts));
        if (monitor.level() != previous) changes.push_back(monitor.level());
        previous = monitor.level();
    }
    CHECK(changes == std::vector<PowerLevel>{PowerLevel::Low, PowerLevel::Critical});
}

TEST_CASE("cutoff: a first reading at or above low is a sound cell, one under it waits for three") {
    CutoffMonitor sound;
    CHECK(sound.apply(sample(kLowMv)) == PowerLevel::Normal);

    CutoffMonitor low;
    CHECK(low.apply(sample(kLowMv - 1)) == PowerLevel::Unknown);
    CHECK(low.apply(sample(kLowMv - 1)) == PowerLevel::Unknown);
    CHECK(low.apply(sample(kLowMv - 1)) == PowerLevel::Low);
}

TEST_CASE("cutoff: a cell on the cable is never low either") {
    CutoffMonitor monitor;
    for (int i = 0; i < 4; i++) monitor.apply(sample(3550));
    REQUIRE(monitor.level() == PowerLevel::Low);
    monitor.apply(sample(3550, /*external_power=*/true));
    CHECK(monitor.level() == PowerLevel::Normal);
}

// Every step of the ladder is read against the one under it, so a pilot meets them in order.
TEST_CASE("cutoff: the ladder is low, critical, boot lockout, flat") {
    CHECK(kLowMv > kCriticalMv);
    CHECK(kCriticalMv > kBootLockoutMv);
    CHECK(kBootLockoutMv > kFlatMv);
    CHECK(kFlatMv > kImplausibleFloorMv);

    // And the gauge reads zero where the device stops, not before it.
    CHECK(kEmptyMv == kFlatMv);
    CHECK(percent_from_mv(kFlatMv) == 0);
    CHECK(percent_from_mv(kCriticalMv) > 0);
    CHECK(percent_from_mv(kBootLockoutMv) > 0);
}

TEST_CASE("cutoff: a floating ADC cannot power the device off") {
    CutoffMonitor monitor;
    for (int i = 0; i < 20; i++) monitor.apply(sample(kImplausibleFloorMv));
    CHECK_FALSE(monitor.cutoff());
    CHECK(monitor.level() == PowerLevel::Unknown);
    CHECK(monitor.implausible() == 20);

    // And a divider that reads zero is the same case, not an empty cell.
    CutoffMonitor disconnected;
    for (int i = 0; i < 20; i++) disconnected.apply(sample(0));
    CHECK_FALSE(disconnected.cutoff());

    // One millivolt above the floor is a reading again, and it does act.
    CutoffMonitor believable;
    for (int i = 0; i < 3; i++) believable.apply(sample(kImplausibleFloorMv + 1));
    CHECK(believable.cutoff());
}

TEST_CASE("cutoff: nothing acts on a terminal the charger is holding up") {
    CutoffMonitor monitor;
    for (int i = 0; i < 10; i++) monitor.apply(sample(3100, /*external_power=*/true));
    CHECK(monitor.level() == PowerLevel::Normal);
    CHECK_FALSE(monitor.cutoff());
}

TEST_CASE("cutoff: the decision latches once it is made") {
    CutoffMonitor monitor;
    for (int i = 0; i < 3; i++) monitor.apply(sample(3100));
    REQUIRE(monitor.cutoff());
    // The radio going quiet lets the cell relax upwards. It is still empty.
    for (int i = 0; i < 10; i++) monitor.apply(sample(4000));
    CHECK(monitor.cutoff());
}

// E1. From Critical down only the record that must survive is written, off the level alone.

TEST_CASE("write gate: a settings write is refused from critical down, the log record is not") {
    CHECK(may_write(PowerLevel::Normal, false, DurableWrite::Settings));
    CHECK_FALSE(may_write(PowerLevel::Critical, false, DurableWrite::Settings));
    CHECK_FALSE(may_write(PowerLevel::Flat, false, DurableWrite::Settings));

    // The one write whose value is highest exactly when the cell is lowest. A
    // landing out with no log is the flight a pilot needed the log for.
    for (const PowerLevel level :
         {PowerLevel::Unknown, PowerLevel::Normal, PowerLevel::Critical, PowerLevel::Flat})
        CHECK(may_write(level, /*supply_warned=*/true, DurableWrite::FlightRecord));
}

// A gauge that never read a believable millivolt is not a reason to make the
// settings page stop working: an unpopulated or unconnected divider is Unknown,
// which the same floor keeps out of the cutoff decision.
TEST_CASE("write gate: a device with no believable reading still writes its settings") {
    CHECK(may_write(PowerLevel::Unknown, false, DurableWrite::Settings));

    CutoffMonitor floating;
    for (int i = 0; i < 20; i++) floating.apply(sample(0));
    REQUIRE(floating.level() == PowerLevel::Unknown);
    CHECK(floating.may_write(DurableWrite::Settings));
}

TEST_CASE("write gate: the monitor answers it from the samples it already has") {
    CutoffMonitor monitor;
    CHECK(monitor.may_write(DurableWrite::Settings));

    for (int i = 0; i < kLevelSamples; i++) monitor.apply(sample(3400));
    REQUIRE(monitor.level() == PowerLevel::Critical);
    CHECK_FALSE(monitor.may_write(DurableWrite::Settings));
    CHECK(monitor.may_write(DurableWrite::FlightRecord));

    // A charger arriving is what makes it writable again: the terminal is held
    // above the cell and nothing may be read into it, so the monitor is Normal.
    for (int i = 0; i < 2; i++) monitor.apply(sample(3400, /*external_power=*/true));
    CHECK(monitor.level() == PowerLevel::Normal);
    CHECK(monitor.may_write(DurableWrite::Settings));
}

TEST_CASE("refresh gate: a critical cell still gets its traffic picture, a flat one does not") {
    CHECK(may_refresh(PowerLevel::Normal, false, PanelRefresh::Routine));
    CHECK(may_refresh(PowerLevel::Critical, false, PanelRefresh::Routine));
    CHECK_FALSE(may_refresh(PowerLevel::Flat, false, PanelRefresh::Routine));

    CHECK(may_refresh(PowerLevel::Unknown, false, PanelRefresh::Routine));
}

TEST_CASE("refresh gate: the white field the glass wears while off is drawn at the cutoff") {
    for (const PowerLevel level :
         {PowerLevel::Unknown, PowerLevel::Normal, PowerLevel::Critical, PowerLevel::Flat})
        CHECK(may_refresh(level, /*supply_warned=*/false, PanelRefresh::Park));
}

TEST_CASE("refresh gate: a fired power-failure comparator stops the park frame too") {
    CHECK_FALSE(may_refresh(PowerLevel::Normal, /*supply_warned=*/true, PanelRefresh::Routine));
    CHECK_FALSE(may_refresh(PowerLevel::Normal, /*supply_warned=*/true, PanelRefresh::Park));

    CutoffMonitor monitor;
    for (int i = 0; i < 4; i++) monitor.apply(sample(4000));
    REQUIRE(monitor.may_refresh(PanelRefresh::Park));
    monitor.on_supply_warning();
    CHECK_FALSE(monitor.may_refresh(PanelRefresh::Park));
    CHECK_FALSE(monitor.may_refresh(PanelRefresh::Routine));
}

// POFCON. The comparator watches the SoC's own rail, which is at or below the
// cell, so by the time it fires the divider's opinion is no longer the question.
TEST_CASE("write gate: a fired power-failure comparator outranks a healthy reading") {
    CutoffMonitor monitor;
    for (int i = 0; i < 4; i++) monitor.apply(sample(4000));
    REQUIRE(monitor.level() == PowerLevel::Normal);
    REQUIRE(monitor.may_write(DurableWrite::Settings));

    monitor.on_supply_warning();
    CHECK(monitor.supply_warned());
    CHECK(monitor.supply_warnings() == 1);
    CHECK_FALSE(monitor.may_write(DurableWrite::Settings));
    CHECK(monitor.may_write(DurableWrite::FlightRecord));

    // Latching: a rail that came back up does not make it un-happen, and a
    // charger does not either.
    for (int i = 0; i < 10; i++) monitor.apply(sample(4100, /*external_power=*/true));
    CHECK_FALSE(monitor.may_write(DurableWrite::Settings));
}

// It refuses writes; it does not power the device off. POFCON warns about VDD,
// well under any healthy cell, so there is no orderly shutdown left to run: the
// panel park alone is kParkMs and the brownout reset is milliseconds away. The
// voltage rule keeps the shutdown, where three consecutive samples are the
// evidence.
TEST_CASE("write gate: the comparator stops writes without switching the device off") {
    CutoffMonitor monitor;
    for (int i = 0; i < 4; i++) monitor.apply(sample(3900));
    monitor.on_supply_warning();
    CHECK_FALSE(monitor.cutoff());
    CHECK(monitor.level() == PowerLevel::Normal);

    monitor.on_supply_warning();
    CHECK(monitor.supply_warnings() == 2);
    CHECK_FALSE(monitor.cutoff());
}
