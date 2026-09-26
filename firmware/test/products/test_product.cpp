// The acceptance invariant on the host: the real product (board, services,
// drivers) links and runs on the host platform with zero framework code. If any
// of it leaks a Zephyr include, this stops compiling.

#include <cstdlib>
#include <string>

#include "core/annunciation/pattern.h"
#include "core/events/link.h"
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/protocol/adsl.h"
#include "doctest/doctest.h"
#include "test/support/product_rig.h"

using namespace skyblip;

TEST_CASE("product: setup brings the radio to Rx and reports its capabilities") {
    Rig rig;
    CHECK(rig.setup() == Status::Ok);
    CHECK(rig.state().started);
    CHECK(ports::has(rig.product.capabilities(), ports::Capability::Rf | ports::Capability::Gnss));
    CHECK(rig.product.degraded() == ports::Capability::None);
}

TEST_CASE("product: a missing optional capability is degraded, a required one refuses") {
    constexpr ports::Capabilities kNoBaro = static_cast<ports::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(ports::Capability::Baro));
    Rig degraded(kNoBaro);
    CHECK(degraded.setup() == Status::Ok);
    CHECK(degraded.product.degraded() == ports::Capability::Baro);

    constexpr ports::Capabilities kNoGnss = static_cast<ports::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(ports::Capability::Gnss));
    Rig refused(kNoGnss);
    CHECK(refused.setup() == Status::Down);
    CHECK_FALSE(refused.state().started);
}

TEST_CASE("product: the radio executor is armed against slot deadlines, not polled") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    const uint32_t before = rig.product.radio().arm_count();
    rig.run(0, 3000);
    // Two band changes per UTC second: O-band uplink dwell, then the M-band slots.
    CHECK(rig.product.radio().arm_count() >= before + 4);
}

TEST_CASE("product: step() runs the service cycle deterministically under a modelled clock") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, 3000);
    uint8_t pkt[6] = {1, 2, 3, 4, 5, 6};
    rig.platform.chips().radio.queue_rx(pkt, sizeof(pkt));
    rig.run(3000, 4200);
    CHECK(rig.state().air.rx_unframed >= 1);  // too short to frame as either system
    CHECK(rig.state().air.rx_bad == 0);
    CHECK(rig.state().traffic.count() == 0);
}

TEST_CASE("product: the e-paper refreshes on change, not on cadence") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, 5000);
    // The boot black and the first radar frame, then a static sky nothing repaints.
    CHECK(rig.platform.chips().epd.present_count == 2);
    CHECK_FALSE(rig.platform.chips().epd.last_full);

    rig.push_fix(1000, 1);  // fix arrives: the radar page changes, one partial
    rig.run(5000, 8000);
    CHECK(rig.platform.chips().epd.present_count == 3);
    CHECK_FALSE(rig.platform.chips().epd.last_full);  // differential, no full
}

TEST_CASE("product: persisted settings are loaded on setup") {
    Rig rig;
    go::Settings s = go::defaults();
    s.alarm_volume = 1;
    uint8_t blob[64];
    go::to_blob(s, blob, sizeof(blob));
    REQUIRE(rig.platform.kv().write("settings", blob, go::blob_size()) == Status::Ok);

    REQUIRE(rig.setup() == Status::Ok);
    CHECK(rig.settings().alarm_volume == 1);
}

// The board is the only thing that knows the address, and only this wiring carries it to a phone.
TEST_CASE("product: the identity a tablet is told is the board's, not a stored setting's") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.raise_link(1);
    rig.run(0, 100);
    rig.send("{\"cmd\":\"get\"}");
    rig.run(100, 200);

    const std::string reply = rig.last_on(events::Endpoint::Config);
    CHECK(reply.find("\"addr\":5987070") != std::string::npos);  // 0x5B5AFE, the host board's
    CHECK(reply.find("\"addr_table\":58") != std::string::npos);
}

TEST_CASE("product: barometric pressure drives vertical speed") {
    Rig rig{kBaroByHand};
    REQUIRE(rig.setup() == Status::Ok);
    CHECK_FALSE(rig.product.ownship().baro_active());

    rig.push_baro(100000, 1000);
    rig.run(1000, 1000);
    CHECK(rig.product.ownship().baro_active());
    rig.push_baro(101000, 3000);
    rig.run(3000, 3000);

    // +10 m in 2 s
    CHECK(rig.state().own.climb_mm_s == doctest::Approx(5000).epsilon(0.05));
}

TEST_CASE("product: a baro sample inside the window is ignored, not extrapolated") {
    Rig rig{kBaroByHand};
    REQUIRE(rig.setup() == Status::Ok);
    rig.push_baro(100000, 1000);
    rig.run(1000, 1000);
    rig.push_baro(199000, 1100);
    rig.run(1100, 1100);
    CHECK(rig.state().own.climb_mm_s == 0);
}

TEST_CASE("product: with no barometer, vertical speed comes from GNSS") {
    Rig rig{kBaroByHand};
    REQUIRE(rig.setup() == Status::Ok);

    rig.push_fix(1000, 1);
    rig.run(1000, 1000);
    rig.push_fix(1010, 2);
    rig.run(3000, 3000);

    CHECK_FALSE(rig.product.ownship().baro_active());
    CHECK(rig.state().own.climb_mm_s == doctest::Approx(5000).epsilon(0.05));
}

TEST_CASE("product: once the barometer speaks, GNSS stops setting vertical speed") {
    Rig rig{kBaroByHand};
    REQUIRE(rig.setup() == Status::Ok);

    rig.push_baro(100000, 500);
    rig.run(500, 500);
    rig.push_baro(100200, 1500);  // +2 m in 1 s
    rig.run(1500, 1500);
    const int32_t from_baro = rig.state().own.climb_mm_s;
    CHECK(from_baro == doctest::Approx(2000).epsilon(0.1));

    rig.push_fix(1000, 1);
    rig.run(2000, 2000);
    rig.push_fix(1200, 2);
    rig.run(4000, 4000);

    CHECK(rig.state().own.climb_mm_s == from_baro);
}

// Once active the barometer stayed in charge for ever, and a dead one froze its last climb on air.
TEST_CASE("product: a barometer that stops answering hands vertical speed back to GNSS") {
    Rig rig{kBaroByHand};
    REQUIRE(rig.setup() == Status::Ok);
    rig.push_baro(100000, 500);
    rig.run(500, 500);
    rig.push_baro(100200, 1500);  // +2 m in 1 s
    rig.run(1500, 1500);
    REQUIRE(rig.product.ownship().baro_active());

    rig.push_fix(1000, 1);
    rig.run(6000, 6000);
    rig.push_fix(1010, 2);
    rig.run(8000, 8000);

    CHECK_FALSE(rig.product.ownship().baro_active());
    CHECK(rig.state().own.climb_valid);
    // +10 m in 2 s of GNSS
    CHECK(rig.state().own.climb_mm_s == doctest::Approx(5000).epsilon(0.05));
}

TEST_CASE("product: with neither a fix nor a barometer the climb is not valid") {
    Rig rig{kBaroByHand};
    REQUIRE(rig.setup() == Status::Ok);
    rig.push_fix(1000, 1);
    rig.run(1000, 1000);
    rig.push_fix(1010, 2);
    rig.run(3000, 3000);
    REQUIRE(rig.state().own.climb_valid);

    rig.product.bus().gnss.push(gnss::GnssSolution{});
    rig.run(4000, 4000);
    CHECK_FALSE(rig.state().own.climb_valid);
}

// A 2D fix differentiated the height it held and sent a climb of zero as valid.
TEST_CASE("product: a 2D fix goes on air with its climb marked unavailable (ADS-L G.1.9)") {
    Rig rig{kBaroByHand};
    REQUIRE(rig.setup() == Status::Ok);
    rig.push_fix(1000, 1);
    rig.run(1000, 1000);
    rig.push_fix(1010, 2);
    rig.run(3000, 3000);
    REQUIRE(rig.state().own.climb_valid);

    rig.push_2d_fix(1010, 3);
    rig.run(4000, 4000);
    CHECK(rig.state().own.fix_valid);
    CHECK_FALSE(rig.state().own.climb_valid);
    protocol::AdslPacket burst{};
    protocol::from_own(burst, rig.state().own, rig.product.board().roles().device_addr, 6, 4);
    CHECK_FALSE(burst.has_climb());
}

// The climb reference outlived a 2D spell, and the height 3D came back with read as a climb.
TEST_CASE("product: the height a 3D fix returns with after a 2D spell is not a climb") {
    Rig rig{kBaroByHand};
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t updates = 0;
    int32_t steepest_mm_s = 0;
    const auto apply_at = [&](uint32_t t) {
        rig.run(t, t);
        const model::OwnState& own = rig.state().own;
        if (own.climb_valid && std::abs(own.climb_mm_s) > steepest_mm_s)
            steepest_mm_s = std::abs(own.climb_mm_s);
    };

    uint32_t t = 1000;
    for (; t <= 3000; t += 1000) {
        rig.push_fix(1000, ++updates);
        apply_at(t);
    }
    for (; t <= 6000; t += 1000) {
        rig.push_2d_fix(1000, ++updates);
        apply_at(t);
    }
    for (; t <= 12000; t += 1000) {
        rig.push_fix(1100, ++updates);
        apply_at(t);
    }

    CHECK(steepest_mm_s == 0);
    CHECK(rig.state().own.climb_valid);
}

TEST_CASE("product: the board reads the cell and the gauge publishes it") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    CHECK_FALSE(rig.state().power.battery.valid);

    rig.platform.battery().millivolts = 3800;
    rig.run(0, 12000);
    CHECK(rig.state().power.battery.valid);
    CHECK(rig.state().power.battery.millivolts == 3800);
    CHECK(rig.state().power.battery.percent == power::percent_from_mv(3800, false));
    CHECK_FALSE(rig.state().power.battery.charging);
    CHECK_FALSE(rig.state().power.battery.external_power);
}

TEST_CASE("product: the same cell on USB power reports a lower state of charge") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.platform.battery().millivolts = 4000;
    rig.run(0, 12000);
    const uint8_t resting = rig.state().power.battery.percent;

    rig.platform.battery().external_power = true;
    rig.run(12000, 24000);
    CHECK(rig.state().power.battery.charging);
    CHECK(rig.state().power.battery.millivolts == 4000);
    CHECK(rig.state().power.battery.percent < resting);
}

TEST_CASE("product: a board with no battery sense says so instead of reporting empty") {
    constexpr ports::Capabilities kNoBattery = static_cast<ports::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(ports::Capability::Battery));
    Rig rig{kNoBattery};
    REQUIRE(rig.setup() == Status::Ok);
    CHECK(rig.product.degraded() == ports::Capability::Battery);

    rig.run(0, 12000);
    CHECK_FALSE(rig.state().power.battery.valid);
    CHECK(rig.state().power.battery.percent == 0);
}

TEST_CASE("product: settings changed over the link are persisted") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.settings().alarm_volume = 5;
    // The only way the device can be told it is on the ground: one stationary
    // solution, which core/flight answers OnGround to and the link's gate reads.
    rig.push_fix(/*alt_m=*/0, /*updates=*/1);
    rig.run(0, 100);

    const char* json = "{\"cmd\":\"set\",\"alarm_volume\":2}";
    events::RxFrame frame{};
    frame.endpoint = events::Endpoint::Config;
    frame.len = static_cast<uint16_t>(__builtin_strlen(json));
    for (uint16_t i = 0; i < frame.len; i++) frame.data[i] = static_cast<uint8_t>(json[i]);
    rig.platform.link().push_rx(frame);
    rig.run(100, 300);
    rig.product.config().config().confirm();
    // The write is a request now, not a call: it coalesces for
    // DurableWriteWindow::kSettleMs and then waits for a phase of the second the
    // NVMC may stall the core in. Two seconds is several of both.
    rig.run(300, 2400);

    uint8_t blob[64];
    size_t n = 0;
    REQUIRE(rig.platform.kv().read("settings", blob, sizeof(blob), n) == Status::Ok);
    go::Settings stored{};
    REQUIRE(go::from_blob(blob, n, stored) == Status::Ok);
    CHECK(stored.alarm_volume == 2);
}

TEST_CASE("product: the reset reason is read once at boot and kept") {
    Rig rig;
    rig.platform.system_power().causes = power::ResetCause::Watchdog | power::ResetCause::Software;
    REQUIRE(rig.setup() == Status::Ok);
    // The bite, not the reset that followed it: that is the whole diagnostic
    // value of the register.
    CHECK(rig.product.reset_reason() == power::ResetReason::Watchdog);

    Rig fresh;
    fresh.platform.system_power().causes = power::ResetCause::PowerOn;
    REQUIRE(fresh.setup() == Status::Ok);
    CHECK(fresh.product.reset_reason() == power::ResetReason::PowerOn);
    // Two different boots must not paint the same page.
    CHECK(fresh.product.boot_page().count_black() != rig.product.boot_page().count_black());
}

// D5. The panel has the reason at boot and then it is gone; the field diagnosis
// happens over the link, days later, with the device in a bag.
TEST_CASE("product: the status reply over the link names why the device came up") {
    Rig rig;
    rig.platform.system_power().causes = power::ResetCause::Watchdog;
    REQUIRE(rig.setup() == Status::Ok);
    rig.platform.link().clear();

    rig.send("{\"cmd\":\"status\"}");
    rig.run(0, 200);
    REQUIRE(rig.platform.link().count_on(events::Endpoint::Config) == 1);
    CHECK(rig.last_on(events::Endpoint::Config).find("WATCHDOG") != std::string::npos);

    // A device that came up because someone pressed the button says that, and
    // not the UNKNOWN a reason nobody passed on would read as.
    Rig pressed;
    pressed.platform.system_power().causes = power::ResetCause::Pin;
    REQUIRE(pressed.setup() == Status::Ok);
    pressed.platform.link().clear();
    pressed.send("{\"cmd\":\"status\"}");
    pressed.run(0, 200);
    CHECK(pressed.last_on(events::Endpoint::Config).find("RESET PIN") != std::string::npos);
    CHECK(pressed.last_on(events::Endpoint::Config).find("UNKNOWN") == std::string::npos);
}

// B3. The slot map is specified against the PPS edge, and the transmit plan is
// armed from it. An edge rebuilt from the millisecond phase is up to a
// millisecond late on a 5 ms guard.
TEST_CASE("product: the PPS edge on the bus is the edge itself, to the microsecond") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);

    rig.platform.clock().set_micros(12345678);
    rig.product.step(12345);
    CHECK(rig.state().clock.pps_locked);
    CHECK(rig.state().clock.pps_edge_us == 12000000u);
    CHECK(rig.state().clock.ms_since_pps == 345u);

    // What the same pass used to publish, rebuilt from the phase: the sub-
    // millisecond remainder was thrown away, and it is not a rounding error but
    // a signed lateness on every plan armed from it.
    const uint64_t rebuilt =
        12345678u - static_cast<uint64_t>(rig.state().clock.ms_since_pps) * 1000;
    CHECK(rebuilt - rig.state().clock.pps_edge_us == 678u);

    // No lock, no edge: a stale instant is worse than none, because the phase
    // it implies is a plausible one.
    rig.platform.pps().set_locked(false);
    rig.platform.clock().set_micros(13345678);
    rig.product.step(13345);
    CHECK_FALSE(rig.state().clock.pps_locked);
    CHECK(rig.state().clock.pps_edge_us == 0u);
}

TEST_CASE("product: the first fix is announced once, then own-ship settles before it flies") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, 500);
    CHECK_FALSE(rig.product.ownship().first_fix().ever_fixed());
    CHECK(int(rig.platform.annunciator().level()) == 0);

    rig.push_fix(1000, 1);
    uint16_t first_note_hz = 0;
    for (uint32_t t = 550; t <= 700; t += 10) {
        rig.platform.clock().set_millis(t);
        rig.product.step(t);
        if (first_note_hz == 0) first_note_hz = rig.platform.annunciator().hz();
    }
    CHECK(rig.product.ownship().first_fix().ever_fixed());
    CHECK(first_note_hz == annunciation::kFirstFixJingle[0].hz);
    // The motor stays out of it: haptics mean traffic that escalated.
    CHECK(rig.platform.annunciator().haptic_ms() == 0);

    // It is short and it stops by itself, so nothing has to remember to silence
    // it before the first traffic contact arrives.
    rig.run(750, 3000);
    CHECK_FALSE(rig.platform.annunciator().sounding());

    // Nothing is worth transmitting yet, and 20 s later it is.
    const gnss::FirstFix& fix = rig.product.ownship().first_fix();
    CHECK_FALSE(fix.settled(3000));
    CHECK_FALSE(fix.settled(fix.fix_since_ms() + gnss::kFirstFixSettleMs - 1));
    CHECK(fix.settled(fix.fix_since_ms() + gnss::kFirstFixSettleMs));

    // A second acquisition is not a first one: no second chirp, and the shorter
    // wait applies.
    gnss::GnssSolution lost{};
    lost.fix_valid = false;
    rig.product.bus().gnss.push(lost);
    rig.run(3050, 3200);
    CHECK_FALSE(fix.settled(3200));
    rig.push_fix(1000, 2);
    rig.run(3250, 3400);
    CHECK_FALSE(rig.platform.annunciator().sounding());
    CHECK(fix.settled(fix.fix_since_ms() + gnss::kRefixSettleMs));
}

// D1. A VBUS event is a wake source on this SoC, so plugging a charger into a
// device that was switched off brings the SoC up. What must not happen is that it
// brings the DEVICE up: a unit that charges itself awake in a flight bag arrives
// flat, and the e-paper holds its last image with the rails down, so nothing about
// it would look wrong until a pilot needed it.
TEST_CASE("product: a charger plugged into a sleeping device is not a boot") {
    Rig rig;
    rig.platform.system_power().causes =
        power::ResetCause::LowPowerWake | power::ResetCause::UsbVbus;
    CHECK(rig.setup() == Status::Ok);
    CHECK(rig.product.boot_path() == power::BootPath::SleepAgain);

    // Nothing became a device: no service loop, and not a pixel on the glass -
    // the panel still holds whatever was painted when it was switched off.
    CHECK_FALSE(rig.state().started);
    CHECK(rig.product.boot_page().count_black() == 0);
    CHECK(rig.platform.chips().epd.present_count == 0);
    // And the page it would have painted names the cause anyway, for the case a
    // bench eye holds the button and looks.
    CHECK(rig.product.reset_reason() == power::ResetReason::ChargerWake);

    // What the shell does with that: the rails go down the same way a long press
    // takes them down, in the order core/power owns.
    rig.platform.system_power().system_off(
        power::button_wake_after_refusal(rig.product.boot_cell()));
    CHECK(rig.platform.system_power().offs == 1);
    CHECK(rig.platform.system_power().order_of(power::PowerDownStep::WakePinArmed) ==
          power::kPowerDownStepCount - 1);
}

TEST_CASE("product: the same charger with the button held is a boot") {
    Rig rig;
    rig.platform.system_power().causes =
        power::ResetCause::LowPowerWake | power::ResetCause::UsbVbus;
    rig.platform.board_gpio().button_down = true;
    REQUIRE(rig.setup() == Status::Ok);
    CHECK(rig.product.boot_path() == power::BootPath::Run);
    CHECK(rig.state().started);
    CHECK(rig.product.boot_page().count_black() > 0);
}

// Every other wake is a device. The one that would be catastrophic and silent is
// a unit refusing to boot the first time its cell is connected.
TEST_CASE("product: a button wake and a first power-on are both a device") {
    Rig pressed;
    pressed.platform.system_power().causes = power::ResetCause::LowPowerWake;
    REQUIRE(pressed.setup() == Status::Ok);
    CHECK(pressed.product.boot_path() == power::BootPath::Run);
    CHECK(pressed.state().started);

    Rig fresh;
    fresh.platform.system_power().causes = power::ResetCause::PowerOn;
    REQUIRE(fresh.setup() == Status::Ok);
    CHECK(fresh.product.boot_path() == power::BootPath::Run);
    CHECK(fresh.state().started);
}

// H. The per-unit trim is applied once, on the way out of the queue, so the gauge
// and the cutoff monitor act on the same millivolts. A cutoff that fired 40 mV
// early on a trimmed unit would be the calibration causing the failure it exists
// to prevent.
TEST_CASE("product: the battery trim reaches the gauge and the cutoff rule together") {
    // This unit reads 60 mV high, measured once on the line against a bench
    // supply and written into settings.
    Rig trimmed;
    go::Settings s = go::defaults();
    s.battery_offset_mv = -60;
    uint8_t blob[64];
    go::to_blob(s, blob, sizeof(blob));
    REQUIRE(trimmed.platform.kv().write("settings", blob, go::blob_size()) == Status::Ok);
    REQUIRE(trimmed.setup() == Status::Ok);
    REQUIRE(trimmed.settings().battery_offset_mv == -60);

    // A reading the raw sample puts above the low-battery warning and the real
    // cell puts below it.
    trimmed.platform.battery().millivolts = 3540;
    trimmed.run(0, 12000);
    CHECK(trimmed.state().power.battery.millivolts == 3480);
    CHECK(trimmed.state().power.battery.percent == power::percent_from_mv(3480, false));
    // The reader that matters: the same trimmed millivolts reached the rule that
    // decides when the device warns and when it goes down.
    CHECK(trimmed.state().power.level == power::PowerLevel::Low);

    // The same board, the same divider, no trim: the gauge and the cutoff rule
    // agree with each other and both are wrong by the same 60 mV.
    Rig raw;
    REQUIRE(raw.setup() == Status::Ok);
    raw.platform.battery().millivolts = 3540;
    raw.run(0, 12000);
    CHECK(raw.state().power.battery.millivolts == 3540);
    CHECK(raw.state().power.level == power::PowerLevel::Normal);
}

// Taken before any service runs, so it is the reader that could have been raw.
TEST_CASE("product: the boot lockout reads the trimmed cell, not the raw divider") {
    go::Settings s = go::defaults();
    s.battery_offset_mv = -60;
    uint8_t blob[64];
    go::to_blob(s, blob, sizeof(blob));

    // 3440 raw is above the lockout, 3380 trimmed is below it.
    Rig trimmed;
    REQUIRE(trimmed.platform.kv().write("settings", blob, go::blob_size()) == Status::Ok);
    trimmed.platform.battery().millivolts = 3440;
    REQUIRE(trimmed.setup() == Status::Ok);
    CHECK(trimmed.product.boot_cell().millivolts == 3380);
    CHECK(trimmed.product.boot_path() == power::BootPath::SleepAgain);

    Rig untrimmed;
    untrimmed.platform.battery().millivolts = 3440;
    REQUIRE(untrimmed.setup() == Status::Ok);
    CHECK(untrimmed.product.boot_path() == power::BootPath::Run);
}

namespace {

std::string status_of(Rig& rig, uint32_t& t) {
    rig.platform.link().clear();
    rig.send("{\"cmd\":\"status\"}");
    rig.run(t, t + 200);
    t += 250;
    return rig.last_on(events::Endpoint::Config);
}

}  // namespace

// J. One reader, on a cadence the sensor deserves. Die temperature is the
// temperature of a lump of plastic in the sun, low-passed by its own mass: it
// moves in minutes, and the measurement is a blocking one-shot on a peripheral
// the radio stack also uses. So it is read from the service pass, ten seconds
// apart, and never from the radio thread.
TEST_CASE("product: the die sensor is read on a slow cadence and reaches the tablet") {
    constexpr ports::Capabilities kWithDie = static_cast<ports::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) |
        static_cast<uint32_t>(ports::Capability::DieTemperature));
    Rig rig{kWithDie};
    REQUIRE(rig.setup() == Status::Ok);
    platform::host::DieTemperature& die = rig.platform.die_temperature();
    die.hold(412);

    uint32_t t = 0;
    rig.run(t, t + 100);
    t += 150;
    // Read on the first pass rather than ten seconds into the boot: the first
    // status a phone is pushed should carry a temperature, and the self-test page
    // is the moment a bench eye is looking.
    CHECK(die.reads() == 1);
    CHECK(rig.product.power().die_temperature_valid());
    CHECK(rig.product.power().die_temperature_dc() == 412);
    CHECK(status_of(rig, t).find("\"die_temp_c\":41") != std::string::npos);

    // Five seconds of passes: still one measurement.
    rig.run(t, t + 5000);
    t += 5050;
    CHECK(die.reads() == 1);

    // Past ten seconds from the first: a second one.
    rig.run(t, t + 6000);
    t += 6050;
    CHECK(die.reads() == 2);
    // And nothing has read it hundreds of times, which is what a per-pass reader
    // would have done by now.
    rig.run(t, t + 30000);
    t += 30050;
    CHECK(die.reads() <= 6);

    // A refused measurement leaves the last good reading standing rather than
    // publishing a zero: the number is minutes old by design anyway.
    const int before = die.reads();
    die.refuse();
    rig.run(t, t + 30000);
    t += 30050;
    CHECK(die.reads() > before);
    CHECK(status_of(rig, t).find("\"die_temp_c\":41") != std::string::npos);
}

// The research prohibits charging in the 72.4 C soak; nothing here could see it.
TEST_CASE("product: a cable in the heat is named, counted and left counted") {
    constexpr ports::Capabilities kWithDie = static_cast<ports::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) |
        static_cast<uint32_t>(ports::Capability::DieTemperature));
    Rig rig{kWithDie};
    REQUIRE(rig.setup() == Status::Ok);
    platform::host::DieTemperature& die = rig.platform.die_temperature();
    die.hold(250);

    uint32_t t = 0;
    rig.run(t, t + 2000);
    t += 2050;
    CHECK(rig.state().power.charge == power::ChargeCondition::Unknown);
    CHECK(rig.product.power().charge_warnings() == 0);

    rig.platform.battery().external_power = true;
    rig.run(t, t + 2000);
    t += 2050;
    CHECK(rig.state().power.charge == power::ChargeCondition::Ok);
    CHECK(rig.product.power().charge_warnings() == 0);

    die.hold(724);
    rig.run(t, t + 15000);
    t += 15050;
    CHECK(rig.state().power.charge == power::ChargeCondition::TooHot);
    // One event, however many passes read the same sensor.
    CHECK(rig.product.power().charge_warnings() == 1);

    rig.platform.battery().external_power = false;
    rig.run(t, t + 2000);
    t += 2050;
    CHECK(rig.state().power.charge == power::ChargeCondition::Unknown);
    CHECK(rig.product.power().charge_warnings() == 1);
}

// The absent case, and it is the host board itself: no sensor, no capability, no
// key. A die reading is not a feature of the product either - nothing the device
// does changes without it - so its absence is not a degraded state, it is a reply
// with one less key in it.
TEST_CASE("product: a board with no die sensor publishes no temperature at all") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    CHECK_FALSE(ports::has(rig.product.capabilities(), ports::Capability::DieTemperature));
    CHECK(rig.product.degraded() == ports::Capability::None);

    uint32_t t = 0;
    rig.run(t, t + 30000);
    t += 30050;
    CHECK_FALSE(rig.product.power().die_temperature_valid());
    CHECK(status_of(rig, t).find("die_temp_c") == std::string::npos);
}

// J. The range gate refuses a reception whose claimed position is further away
// than this radio could have heard it. Whether that fires once a week or once a
// second is the first question a support case asks, so the counter leaves the
// device: read off the table that keeps it, on its own reply.
TEST_CASE("product: the range gate's refusals leave the device over the link") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.seconds(t, 3, /*speed_mm_s=*/0, 0);
    REQUIRE(rig.state().own.fix_valid);

    rig.platform.link().clear();
    rig.send("{\"cmd\":\"radio\"}");
    rig.run(t, t + 200);
    t += 250;
    CHECK(rig.last_on(events::Endpoint::Config).find("\"range_refused\":0") != std::string::npos);

    // A contact on the other side of the country, claimed by a frame that passed
    // its CRC. Pushed at the table's own door, which is where the gate lives.
    model::AircraftObs ghost{};
    ghost.addr = 0x00ABCDEF;
    ghost.addr_table = 5;
    ghost.position_valid = true;
    ghost.lat_1e7 = rig.state().own.lat_1e7 + 50000000;  // five degrees north
    ghost.lon_1e7 = rig.state().own.lon_1e7;
    ghost.source = model::Source::AdslDirect;
    REQUIRE(rig.state().traffic.update(ghost, 0) < 0);
    REQUIRE(rig.state().traffic.implausible_count() == 1);

    rig.platform.link().clear();
    rig.send("{\"cmd\":\"radio\"}");
    rig.run(t, t + 200);
    CHECK(rig.platform.link().last().bytes.find("\"range_refused\":1") != std::string::npos);
}

// M. Vertical speed and turn rate are both differences over a window kept on
// ports::Clock::millis(), and both keep the instant they last sampled at in a
// uint32_t whose zero is biased away rather than flagged (ownship.cpp: a stamp of
// zero would mean "no reference yet", and the counter produces exactly one zero
// per wrap). The windows themselves are unsigned differences, so this is what a
// climb through the wrap instant looks like: a climb, measured over its window,
// not a 4.29-billion-millisecond interval that reads as no climb at all.
TEST_CASE("product: vertical speed is measured across the 49.7-day wrap") {
    Rig rig{kBaroByHand};
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0u - 500u;  // half a second short of the wrap

    rig.push_baro(100000, t);
    rig.run_span(t, 0);
    CHECK(rig.product.ownship().baro_active());

    // +10 m over the second that straddles zero.
    const uint32_t after = t + 950u;
    rig.push_baro(101000, after);
    t = after;
    rig.run_span(t, 0);
    CHECK(rig.state().own.climb_mm_s == doctest::Approx(10000).epsilon(0.1));

    // And a sample inside the window is still refused after the wrap, rather than
    // being taken as a 49-day interval that reads as no climb at all.
    rig.push_baro(199000, t);
    rig.run_span(t, 0);
    CHECK(rig.state().own.climb_mm_s == doctest::Approx(10000).epsilon(0.1));
}

// M. Where the radio believes it is inside the second, across the 49.7-day wrap of
// ports::Clock::millis(). With PPS locked the phase is measured from the latched
// edge, which is a 64-bit microsecond figure and cannot wrap in the life of the
// device. Without it the phase used to be now_ms % 1000, and that is not a phase at
// all: 2^32 ms is 4294967.296 seconds, so at the wrap the free-running second
// stepped 705 ms BACKWARDS and the dwell map was armed out of order for a second -
// with the anchor already lost, which is the worst moment to add a fault. Both
// branches read micros() now.
//
// The clock is driven in microseconds here because that is what the silicon does:
// now_ms is the low 32 bits of the same uptime, so millis() wraps underneath a
// micros() that keeps counting.
TEST_CASE("product: the free-running dwell phase steps forward through the 49.7-day wrap") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint64_t us = static_cast<uint64_t>(0xFFFFFF00u) * 1000u;  // 256 ms short of the wrap

    // The receiver has no PPS lock, so the case is about the free-running fallback
    // and not about the anchored path.
    rig.platform.pps().set_locked(false);
    rig.run_span_from_us(us, 0);
    REQUIRE_FALSE(rig.state().clock.pps_locked);

    int previous = rig.state().rf.dwell.phase_ms;
    bool wrapped = false;
    for (int step = 0; step < 20; step++) {
        const uint32_t millis_before = rig.platform.clock().millis();
        rig.run_span_from_us(us, 0);
        if (rig.platform.clock().millis() < millis_before) wrapped = true;
        // 50 ms of clock is 50 ms of phase, all the way round the second and all
        // the way through the counter turning over.
        CHECK(rig.state().rf.dwell.phase_ms == (previous + 50) % 1000);
        previous = rig.state().rf.dwell.phase_ms;
    }
    CHECK(wrapped);
}

// A pilot on the apron asks how long this will take, and NO FIX is the same word
// for a receiver reading satellites and one that has said nothing at all.
TEST_CASE("product: with no fix the bus says how far the receiver has got") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);

    rig.run(0, 100);
    CHECK(rig.state().gnss.stage == gnss::Stage::Silent);

    gnss::GnssSolution looking{};
    rig.product.bus().gnss.push(looking);
    rig.run(150, 200);
    CHECK(rig.state().gnss.stage == gnss::Stage::Blind);

    // A date is read off a satellite, so it is the first evidence of one.
    gnss::GnssSolution dated{};
    dated.utc_valid = true;
    dated.utc = Rig::kUtcBase;
    rig.product.bus().gnss.push(dated);
    rig.run(250, 300);
    CHECK(rig.state().gnss.stage == gnss::Stage::Solving);
    CHECK(rig.state().gnss.stage_s == 0u);

    rig.push_timed_fix(0, 500);
    rig.run(350, 400);
    CHECK(rig.state().gnss.stage == gnss::Stage::Fixed);

    // A receiver that stops talking is silent again, whatever it last managed.
    rig.run(450, 450 + gnss::kSentenceMaxAgeMs);
    CHECK(rig.state().gnss.stage == gnss::Stage::Silent);
}
