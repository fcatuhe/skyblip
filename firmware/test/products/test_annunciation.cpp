// What the buzzer does in a cockpit, through the whole product: virtual
// aircraft transmit real ADS-L frames, the production receive path decodes
// them, the tracker grades them and the alarm service drives the annunciator
// the board is holding. Nothing below the services is stubbed.
//
// The bug: the annunciator's alarm() opens a CONTINUOUS tone and the service
// only ever closed it when the level fell to zero, so it sounded for as long as
// anything stayed inside the advisory window, with no cadence at all - one
// tone, started once, released when the sky emptied. In a thermal that is the
// reason a pilot switches the device off.
#include "core/annunciation/annunciator.h"
#include "core/power/shutdown.h"
#include "doctest/doctest.h"
#include "simulator/simulator.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

constexpr uint32_t kStepMs = simulator::Simulator::kStepMs;

struct Sky {
    simulator::Simulator simulator{};

    platform::host::Annunciator& buzzer() { return simulator.platform().annunciator(); }
    uint32_t tone_commands() { return buzzer().tone_commands(); }
    uint8_t sounding_level() { return simulator.buzzer_level(); }
    traffic::Level announcing_level() { return simulator.announcing_level(); }

    void run(uint32_t from, uint32_t to) {
        for (uint32_t t = from; t <= to; t += kStepMs) simulator.step(t);
    }

    // A device flying along with one converging glider on the M-band: an
    // advisory within a couple of seconds of the first frames being decoded.
    uint32_t with_a_contact_in_the_window() {
        REQUIRE(simulator.setup() == Status::Ok);
        run(0, 2000);
        simulator.world().add_threat();
        run(2000, 6000);
        REQUIRE(int(announcing_level()) == 1);
        return 6000;
    }

    // One more aircraft entering the window, which is the only thing that makes
    // this device speak twice. Returns the instant the pair is sounding, so a
    // release has something to release, or 0 if it never sounded.
    uint32_t sounding_again(uint32_t from) {
        simulator.world().add_aircraft(1500, 500, 0, 30, 180);
        return step_until_sounding(from, from + 6000);
    }

    // Step until the buzzer is actually mid-pulse, so a release has something
    // to release. Returns the instant, or 0 if the pattern never sounded.
    uint32_t step_until_sounding(uint32_t from, uint32_t to) {
        for (uint32_t t = from; t <= to; t += kStepMs) {
            simulator.step(t);
            if (sounding_level() > 0) return t;
        }
        return 0;
    }
};

}  // namespace

TEST_CASE("product: an advisory is two calls, and the contact standing says nothing more") {
    Sky sky;
    uint32_t t = sky.with_a_contact_in_the_window();

    // The calls are one PWM start per note and not one more, however many passes
    // the loop makes: a tone re-armed on every pass is a click, not a tone.
    const uint32_t before = sky.tone_commands();
    sky.run(t, t + 20000);
    t += 20000;
    CHECK(sky.tone_commands() == before);
    CHECK(int(sky.sounding_level()) == 0);

    // It is still the contact the device is announcing, and it is still plotted.
    CHECK(int(sky.announcing_level()) == 1);
    CHECK(sky.simulator.product().state().traffic.count() == 1);
}

TEST_CASE("product: an empty sky releases the buzzer and does not re-announce anything") {
    Sky sky;
    uint32_t t = sky.with_a_contact_in_the_window();
    REQUIRE(sky.tone_commands() > 0);

    // Nothing transmits any more. The target ages out of the alert window
    // first, and the traffic table drops it later, on its own schedule.
    sky.simulator.world().clear_aircraft();
    sky.run(t, t + 10000);
    t += 10000;
    CHECK(int(sky.announcing_level()) == 0);
    CHECK(int(sky.sounding_level()) == 0);

    const uint32_t before = sky.tone_commands();
    sky.run(t, t + 20000);
    CHECK(sky.tone_commands() == before);
    CHECK(int(sky.sounding_level()) == 0);
}

// The pad's long touch is a pilot saying they have the aircraft in sight.
TEST_CASE("product: a long touch of the pad dismisses a standing alarm") {
    Sky sky;
    uint32_t t = sky.with_a_contact_in_the_window();

    sky.simulator.world().hold_pad(true);
    sky.run(t, t + go::Controls::kLongTouchHoldMs + 500);
    t += go::Controls::kLongTouchHoldMs + 500;
    sky.simulator.world().hold_pad(false);
    sky.run(t, t + 500);
    t += 500;

    CHECK(int(sky.announcing_level()) == 0);
    CHECK(int(sky.sounding_level()) == 0);
    CHECK(int(sky.simulator.product().state().alarm_undismissed) == 0);
    // The lamp goes with the buzzer: nothing is left saying look.
    CHECK(sky.simulator.product().alarm().indicator_condition() != indication::Condition::Alarm);
    // Still an advisory, still plotted: what was dismissed is the saying, not the sky.
    CHECK(int(sky.simulator.product().state().alarm_level) == 1);

    const uint32_t before = sky.tone_commands();
    sky.run(t, t + 8000);
    CHECK(sky.tone_commands() == before);
}

TEST_CASE("product: switching alarms off silences the buzzer and stills the motor on that pass") {
    Sky sky;
    models::Drv2605& chip = sky.simulator.platform().chips().haptic;
    uint32_t t = sky.with_a_contact_in_the_window();

    // Mid-pulse, which is the case that matters: a setting that only takes
    // effect at the end of a pattern is a setting a pilot does not believe.
    t = sky.sounding_again(t);
    REQUIRE(t > 0);
    REQUIRE(chip.moving());
    const uint32_t silences = sky.buzzer().silences();

    sky.simulator.product().settings().alarm_enabled = false;
    sky.simulator.step(t + kStepMs);
    CHECK(int(sky.sounding_level()) == 0);
    CHECK(sky.buzzer().silences() == silences + 1);
    CHECK_FALSE(chip.moving());
    CHECK(sky.buzzer().guard_cuts() == 0);

    // Silent while it is off, with the threat still closing.
    const uint32_t before = sky.tone_commands();
    sky.run(t, t + 10000);
    CHECK(sky.tone_commands() == before);
    CHECK(int(sky.announcing_level()) == 0);

    // And audible again the moment the pilot turns it back on.
    sky.simulator.product().settings().alarm_enabled = true;
    sky.run(t + 10000, t + 13000);
    CHECK(sky.tone_commands() > before);
}

TEST_CASE("product: soft or loud, the advisory is called at that loudness and felt") {
    for (const go::AlarmVolume volume : {go::AlarmVolume::Soft, go::AlarmVolume::Loud}) {
        Sky sky;
        REQUIRE(sky.simulator.setup() == Status::Ok);
        sky.simulator.product().settings().alarm_volume = volume;
        sky.run(0, 2000);
        sky.simulator.world().add_threat();
        REQUIRE(sky.step_until_sounding(2000, 8000) > 0);

        CHECK(sky.buzzer().pitch_hz() == annunciation::kNoteA7Hz);
        CHECK(sky.buzzer().motor_starts() == 1);
        CHECK(sky.buzzer().vibrating());
        CHECK(sky.buzzer().loudness() ==
              (volume == go::AlarmVolume::Soft ? ports::Loudness::Soft : ports::Loudness::Loud));
    }
}

TEST_CASE("product: volume off silences the buzzer and the motor both") {
    Sky sky;
    REQUIRE(sky.simulator.setup() == Status::Ok);
    sky.simulator.product().settings().alarm_volume = go::AlarmVolume::Off;
    sky.run(0, 2000);
    sky.simulator.world().add_threat();
    sky.run(2000, 8000);

    CHECK(sky.simulator.product().state().alarm_undismissed == traffic::Level::Advisory);
    CHECK(sky.buzzer().motor_starts() == 0);
    CHECK(sky.tone_commands() == 0);
    CHECK_FALSE(sky.simulator.product().alarm().sounding());
}

TEST_CASE("product: a device on its way down does not leave the buzzer sounding") {
    Sky sky;
    uint32_t t = sky.with_a_contact_in_the_window();

    t = sky.sounding_again(t);
    REQUIRE(t > 0);

    // The same request a long press makes. From here the service loop stops
    // running, so whatever the buzzer was doing is whatever it would go on
    // doing until the rails drop.
    REQUIRE(sky.simulator.platform().chips().haptic.moving());
    sky.simulator.product().shutdown().request(power::ShutdownReason::LongPress, t);
    sky.simulator.step(t + kStepMs);
    CHECK_FALSE(sky.simulator.platform().chips().haptic.moving());
    REQUIRE(sky.simulator.product().shutdown().going_down());
    CHECK(int(sky.sounding_level()) == 0);
    CHECK_FALSE(sky.simulator.product().alarm().sounding());

    const uint32_t before = sky.tone_commands();
    sky.run(t, t + 20000);
    CHECK(sky.tone_commands() == before);
    CHECK(int(sky.sounding_level()) == 0);
    CHECK(int(sky.announcing_level()) == 0);
    // The panel's own peripheral goes the same way: nothing is left driven.
    CHECK_FALSE(sky.simulator.backlight());
}

TEST_CASE("product: a standing advisory shakes the motor with its two calls, and once only") {
    Sky sky;
    uint32_t t = sky.with_a_contact_in_the_window();
    // The motor says what the buzzer says, and nothing else: a motor pulsing in
    // a pocket for as long as a glider shares the thermal would be a pilot's
    // whole flight.
    const uint32_t pulses = sky.buzzer().motor_starts();
    CHECK(pulses == 2);
    CHECK(sky.buzzer().guard_cuts() == 0);

    sky.run(t, t + 20000);
    CHECK(sky.buzzer().motor_starts() == pulses);
}

// A unit at the hold beeped for every aircraft that climbed out of the field or turned in above it.
TEST_CASE("product: a unit on the ground says nothing of the circuit, and one thing at liftoff") {
    Sky sky;
    sky.simulator.world().set_speed_kt(0);
    REQUIRE(sky.simulator.setup() == Status::Ok);
    sky.run(0, 5000);

    // Two aircraft circling the field, inside the advisory ring and outside formation range.
    sky.simulator.world().add_aircraft(1500, 0, 150, 35, 270, -1, -1, protocol::System::AdslDirect,
                                       -1.34);
    sky.simulator.world().add_aircraft(0, 2000, 200, 35, 0, -1, -1, protocol::System::AdslDirect,
                                       -1.0);
    const uint32_t pulses = sky.buzzer().motor_starts();
    const uint32_t tones = sky.tone_commands();
    sky.run(5000, 45000);
    CHECK(sky.simulator.product().state().traffic.count() == 2);
    CHECK(sky.buzzer().motor_starts() == pulses);
    CHECK(sky.tone_commands() == tones);

    sky.simulator.world().set_speed_kt(90);
    sky.run(45000, 60000);
    CHECK(sky.buzzer().motor_starts() == pulses + 2);
    CHECK(int(sky.announcing_level()) == 1);
}

TEST_CASE("product: the first fix plays its tune, and traffic takes the buzzer off it") {
    Sky sky;
    REQUIRE(sky.simulator.setup() == Status::Ok);

    uint32_t started = 0;
    uint16_t first_note_hz = 0;
    uint16_t lowest_hz = 0xFFFF, highest_hz = 0;
    uint32_t sounding_ms = 0;
    for (uint32_t t = 0; t <= 5000; t += kStepMs) {
        sky.simulator.step(t);
        if (!sky.buzzer().sounding()) continue;
        const uint16_t hz = sky.buzzer().pitch_hz();
        if (started == 0) {
            started = t;
            first_note_hz = hz;
        }
        if (hz < lowest_hz) lowest_hz = hz;
        if (hz > highest_hz) highest_hz = hz;
        sounding_ms += kStepMs;
    }
    uint32_t tone_ms = 0;
    for (const annunciation::Note& note : annunciation::kFirstFixJingle) tone_ms += note.tone_ms;

    CHECK(sky.tone_commands() == annunciation::kFirstFixTune.count);
    CHECK(started > 0);
    CHECK(first_note_hz == annunciation::kNoteE7Hz);
    CHECK(lowest_hz == annunciation::kNoteC7Hz);
    CHECK(highest_hz == annunciation::kNoteG7Hz);
    CHECK(sounding_ms >= tone_ms - annunciation::kFirstFixTune.count * kStepMs);
    CHECK(sounding_ms <= tone_ms);
    // No haptics: the motor is reserved for traffic, so a pilot who feels it
    // knows what it means without looking.
    CHECK(sky.buzzer().motor_starts() == 0);
    CHECK(int(sky.sounding_level()) == 0);
    CHECK(sky.buzzer().silences() == annunciation::kFirstFixTune.count);

    // Traffic then owns it, and the fix is not chirped a second time.
    sky.simulator.world().add_threat();
    sky.run(5000, 9000);
    CHECK(int(sky.announcing_level()) == 1);
}

// ---------------------------------------------------------------------------
// The haptic, all the way down to the registers.
//
// Every case above counts pulses at the role. That counter was true and the
// device still did not vibrate: the annunciator drove P0.08 as a plain GPIO, and
// on a T-Echo Plus that pin is a DRV2605's enable. These drive the whole product
// and then read the chip.
// ---------------------------------------------------------------------------

TEST_CASE("product: an announcement reaches the haptic driver's registers") {
    Sky sky;
    models::Drv2605& chip = sky.simulator.platform().chips().haptic;

    // Configured at bring-up and idle: standby, not a pin sitting high.
    REQUIRE(sky.simulator.setup() == Status::Ok);
    CHECK(chip.standby());
    CHECK_FALSE(chip.moving());

    sky.run(0, 2000);
    sky.simulator.world().add_threat();

    int starts = 0;
    bool was_moving = false;
    for (uint32_t t = 2000; t <= 8000; t += kStepMs) {
        sky.simulator.step(t);
        if (chip.moving() && !was_moving) starts++;
        was_moving = chip.moving();
    }
    CHECK(int(sky.announcing_level()) == 1);
    // The pulse was made over the bus: a mode, a drive value and a stop. The
    // enable pin is not wired into the host's virtual GPIO at all
    // (hardware/platform/host/io.h), so nothing here could have moved the motor
    // by driving P0.08 - which is what the annunciator used to do.
    // One start per call: the rest between them reaches the part as a stop.
    CHECK(starts == 2);
    CHECK(sky.buzzer().motor_starts() == 2);
}

TEST_CASE("product: the motor is not left running after its calls") {
    Sky sky;
    models::Drv2605& chip = sky.simulator.platform().chips().haptic;
    uint32_t t = sky.with_a_contact_in_the_window();

    // A motor left on is a flat battery: well past the advisory, the driver is back in standby.
    sky.run(t, t + 3000);
    CHECK_FALSE(chip.moving());
    CHECK(chip.standby());
    CHECK(sky.buzzer().guard_cuts() == 0);
}

TEST_CASE("product: a unit with no haptic driver flies, sounds, and says what is missing") {
    // The same board with nothing at 0x5A: an empty pad, a dead part, or a plain
    // T-Echo that came down the line as a Plus.
    constexpr ports::Capabilities kNoHaptic = static_cast<ports::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(ports::Capability::Haptic));
    Rig rig{kNoHaptic};
    REQUIRE(rig.setup() == Status::Ok);

    // Optional, so the device flies and says so once.
    CHECK(rig.product.flyable());
    CHECK_FALSE(ports::has(rig.product.capabilities(), ports::Capability::Haptic));
    CHECK(ports::has(rig.product.degraded(), ports::Capability::Haptic));

    // And the voice it does have still works: the first fix is chirped by the
    // same service that would have pulsed the motor.
    uint32_t t = 0;
    rig.push_timed_fix(/*speed_mm_s=*/0, /*altitude_msl_m=*/300);
    rig.run(t, t + 1000);
    CHECK(rig.platform.annunciator().tone_commands() >= 1);
    CHECK(rig.platform.chips().haptic.moving() == false);
}
