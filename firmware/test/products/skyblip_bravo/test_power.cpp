// The ways a Bravo goes dark, every part parked on each: the X1 items #170 moved to #176.
#include "doctest/doctest.h"
#include "test/support/bravo_rig.h"

using namespace skyblip;

namespace {

void run_until_off(BravoRig& rig) {
    for (int i = 0; i < 2000 && !rig.product.ready_to_power_off(); i++) rig.run_for(10);
}

void check_parked(BravoRig& rig) {
    CHECK(rig.product.board().receiver().parked());
    CHECK(rig.platform.radio().mode == models::Lr2021::Mode::Sleep);
    CHECK_FALSE(rig.platform.indicator().lit());
    CHECK_FALSE(rig.platform.annunciator().sounding());
    CHECK(rig.platform.haptic().standby());
}

}  // namespace

TEST_CASE("bravo: a long press switches it off with the receiver asleep and every output parked") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(2);
    rig.hold_button(power::kLongPressHoldMs + 200);
    REQUIRE(rig.product.shutdown().reason() == power::ShutdownReason::LongPress);
    run_until_off(rig);
    REQUIRE(rig.product.ready_to_power_off());
    check_parked(rig);
}

TEST_CASE("bravo: a cable plugged into a unit switched off goes straight back to sleep") {
    BravoRig rig;
    rig.platform.system_power().causes =
        power::ResetCause::LowPowerWake | power::ResetCause::UsbVbus;
    rig.platform.battery().external_power = true;
    REQUIRE(rig.setup() == Status::Ok);
    REQUIRE(rig.product.boot_path() == power::BootPath::SleepAgain);

    bool parked = false;
    for (uint32_t now_ms = 0; now_ms < 2000 && !parked; now_ms += 10) {
        rig.platform.clock().set_milliseconds(now_ms);
        parked = rig.product.park_refusal(now_ms);
    }
    CHECK(parked);
    CHECK_FALSE(rig.platform.indicator().lit());
    CHECK_FALSE(rig.platform.annunciator().sounding());
    CHECK_FALSE(rig.platform.link().advertise_calls > 0);
}

TEST_CASE("bravo: a press wakes it, even with the cable in") {
    BravoRig rig;
    rig.platform.system_power().causes =
        power::ResetCause::LowPowerWake | power::ResetCause::UsbVbus;
    rig.platform.battery().external_power = true;
    rig.platform.set_button_down(true);
    REQUIRE(rig.setup() == Status::Ok);
    CHECK(rig.product.boot_path() == power::BootPath::Run);
    CHECK(rig.product.flyable());
}

TEST_CASE("bravo: a receiver that never confirms its sleep is held in reset past the ceiling") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(1);
    rig.platform.radio().busy_stuck = true;
    rig.hold_button(power::kLongPressHoldMs + 200);
    run_until_off(rig);
    CHECK(rig.product.ready_to_power_off());
    CHECK(rig.platform.radio().in_reset);
}

// GPT's review: a receiver that never started reported parked while the LR2021 stayed in standby.
TEST_CASE("bravo: a radio whose probe failed is put to sleep on the way down, not skipped") {
    BravoRig rig([](platform::host::MeshtrackerX1& x1) { x1.radio().version_major = 2; });
    REQUIRE(rig.setup() == Status::Down);
    REQUIRE_FALSE(ports::has(rig.product.capabilities(), ports::Capability::AdsbReceiver));
    CHECK_FALSE(rig.product.board().receiver().parked());

    rig.run_for(100);
    rig.hold_button(power::kLongPressHoldMs + 200);
    run_until_off(rig);
    REQUIRE(rig.product.ready_to_power_off());
    CHECK(rig.product.board().receiver().parked());
    CHECK(rig.platform.radio().mode == models::Lr2021::Mode::Sleep);
}

TEST_CASE(
    "bravo: a radio whose probe failed and whose BUSY sticks is held in reset on the way down") {
    BravoRig rig([](platform::host::MeshtrackerX1& x1) { x1.radio().version_major = 2; });
    REQUIRE(rig.setup() == Status::Down);
    rig.platform.radio().busy_stuck = true;
    rig.run_for(100);
    rig.hold_button(power::kLongPressHoldMs + 200);
    run_until_off(rig);
    REQUIRE(rig.product.ready_to_power_off());
    CHECK(rig.product.board().receiver().parked());
    CHECK(rig.platform.radio().in_reset);
}

TEST_CASE("bravo: a platform that fails its bring-up still parks the radio it probed") {
    BravoRig rig;
    rig.platform.begin_result = Status::Down;
    REQUIRE(rig.setup() == Status::Down);
    rig.run_for(100);
    rig.hold_button(power::kLongPressHoldMs + 200);
    run_until_off(rig);
    REQUIRE(rig.product.ready_to_power_off());
    CHECK(rig.platform.radio().mode == models::Lr2021::Mode::Sleep);
}

TEST_CASE("bravo: a refused cable wake never brings the receiver, the link or the settings up") {
    BravoRig rig;
    rig.platform.system_power().causes =
        power::ResetCause::LowPowerWake | power::ResetCause::UsbVbus;
    rig.platform.battery().external_power = true;
    REQUIRE(rig.setup() == Status::Ok);
    REQUIRE(rig.product.boot_path() == power::BootPath::SleepAgain);
    CHECK(rig.platform.radio().count(parts::lr2021::kSetRx) == 0);
    CHECK(rig.platform.begins == 0);
}

// GPT's re-review: the board probed the LR2021 and raised the haptic enable from its constructor,
// before the watchdog and before a cable wake was refused.
TEST_CASE("bravo: building the product touches no part") {
    BravoRig rig;
    CHECK(rig.platform.radio().commands_seen.empty());
    CHECK(rig.platform.radio().reset_pulses == 0);
    CHECK_FALSE(rig.platform.radio().in_reset);
    CHECK_FALSE(rig.platform.haptic().enable_driven);
    CHECK(rig.platform.haptic().enable_raises == 0);
}

namespace {

struct RefusedBoot {
    const char* name;
    power::ResetCause causes;
    uint16_t divider_mv;
    bool external_power;
};

}  // namespace

// Every road power::boot_path() refuses on: the radio's sleep is unknown after any reset, so it is
// held in reset through GPIO, and no SPI command, probe or haptic enable is spent on a refusal.
TEST_CASE("bravo: every refused boot holds the radio in reset and talks to no part") {
    const RefusedBoot refusals[] = {
        {"a cable wake from SYSTEM OFF",
         power::ResetCause::LowPowerWake | power::ResetCause::UsbVbus, 4050, true},
        {"a cold start on a low cell", power::ResetCause::PowerOn, 3350, false},
        {"a press from SYSTEM OFF on a low cell", power::ResetCause::LowPowerWake, 3350, false},
        {"a watchdog restart on a flat cell", power::ResetCause::Watchdog, 3150, false},
    };
    for (const RefusedBoot& refusal : refusals) {
        CAPTURE(refusal.name);
        BravoRig rig;
        rig.platform.system_power().causes = refusal.causes;
        rig.platform.battery().divider_mv = refusal.divider_mv;
        rig.platform.battery().external_power = refusal.external_power;
        REQUIRE(rig.setup() == Status::Ok);
        REQUIRE(rig.product.boot_path() == power::BootPath::SleepAgain);
        bool parked = false;
        for (uint32_t now_ms = 0; now_ms < 2000 && !parked; now_ms += 10) {
            rig.platform.clock().set_milliseconds(now_ms);
            parked = rig.product.park_refusal(now_ms);
        }
        CHECK(parked);
        CHECK(rig.platform.radio().in_reset);
        CHECK(rig.platform.radio().commands_seen.empty());
        CHECK(rig.platform.radio().reset_pulses == 0);
        CHECK_FALSE(rig.platform.haptic().enable_driven);
        CHECK(rig.platform.haptic().enable_raises == 0);
        CHECK(rig.platform.begins == 0);
    }
}
