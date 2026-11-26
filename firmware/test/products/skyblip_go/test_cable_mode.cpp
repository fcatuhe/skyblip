// Cable mode: a Go wired to the aircraft's USB socket switches on and off with the master switch.
#include "core/power/battery.h"
#include "core/power/cable.h"
#include "core/power/shutdown.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/settings.h"
#include "test/support/product_rig.h"
#include "test/support/update_rig.h"

using namespace skyblip;

namespace {

void store_power_switch(Rig& rig, power::Switch power_switch) {
    go::Settings settings = go::defaults();
    settings.power_switch = power_switch;
    uint8_t blob[64];
    go::to_blob(settings, blob, sizeof(blob));
    REQUIRE(rig.platform.key_value().write("settings", blob, go::blob_size()) == Status::Ok);
}

void pull_cable_for(Rig& rig, uint32_t span_ms) {
    rig.platform.battery().external_power = false;
    rig.run_for(span_ms, 1000);
}

void install_with_the_cable_pulled(Rig& before) {
    store_power_switch(before, power::Switch::Cable);
    stage_versions(before);
    before.platform.battery().external_power = true;
    REQUIRE(before.setup() == Status::Ok);
    before.run_for(5000);
    before.platform.battery().external_power = false;
    install_and_swap(before);
    REQUIRE(before.platform.dfu().triggered == 1);
}

}  // namespace

TEST_CASE("product: in cable mode a cable gone for the hold switches the Go off for the cable" *
          doctest::test_suite("main")) {
    Rig rig;
    store_power_switch(rig, power::Switch::Cable);
    rig.platform.battery().external_power = true;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run_for(5000);

    CHECK_FALSE(rig.state().power.cable_off_in_ms);
    pull_cable_for(rig, power::kCableGoneHoldMs - 5000);
    CHECK_FALSE(rig.product.shutdown().going_down());
    REQUIRE(rig.state().power.cable_off_in_ms);
    // The hold starts on the first battery sample without the cable, up to one period late.
    CHECK(*rig.state().power.cable_off_in_ms <= 5000 + power::kBatteryPeriodMs);

    pull_cable_for(rig, 10000);
    REQUIRE(rig.product.shutdown().going_down());
    CHECK(rig.product.shutdown().reason() == power::ShutdownReason::CableGone);

    rig.run_for(10000);
    REQUIRE(rig.product.ready_to_power_off());
    CHECK_FALSE(rig.product.cable_returned());
    rig.platform.system_power().system_off(rig.product.power_switch());
    CHECK(rig.platform.system_power().off_switch == power::Switch::Cable);
}

// A charger left in a flight bag must still wake nothing, and a pulled charger stops nothing.
TEST_CASE("product: on the button a pulled cable never switches the Go off") {
    Rig rig;
    rig.platform.battery().external_power = true;
    REQUIRE(rig.setup() == Status::Ok);
    REQUIRE(rig.product.power_switch() == power::Switch::Button);
    rig.run_for(5000);

    pull_cable_for(rig, 2 * power::kCableGoneHoldMs);
    CHECK_FALSE(rig.product.shutdown().going_down());
    CHECK_FALSE(rig.state().power.cable_off_in_ms);
}

TEST_CASE("product: in cable mode the cable wakes the Go, and it signs on as a press would" *
          doctest::test_suite("main")) {
    Rig rig;
    store_power_switch(rig, power::Switch::Cable);
    rig.platform.battery().external_power = true;
    rig.platform.system_power().causes = power::ResetCause::UsbVbus | power::ResetCause::PowerOn;
    REQUIRE(rig.setup() == Status::Ok);
    CHECK(rig.product.boot_path() == power::BootPath::Run);
    CHECK(rig.product.reset_reason() == power::ResetReason::ChargerWake);
    CHECK(rig.state().started);

    rig.run(0, 100, 10);
    CHECK(rig.platform.indicator().lamp() == indication::Colour::Green);
    CHECK(rig.platform.annunciator().vibrating());
}

// SYSTEM OFF wakes on VBUS rising, so a cable already back when the rails drop would wake nothing.
TEST_CASE("product: a cable back while the Go parks for its loss restarts it instead") {
    Rig rig;
    store_power_switch(rig, power::Switch::Cable);
    rig.platform.battery().external_power = true;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run_for(5000);

    pull_cable_for(rig, power::kCableGoneHoldMs + 2000);
    REQUIRE(rig.product.shutdown().reason() == power::ShutdownReason::CableGone);
    rig.platform.battery().external_power = true;
    rig.run_for(10000);
    REQUIRE(rig.product.ready_to_power_off());
    CHECK(rig.product.cable_returned());

    // A long press with the cable in is the pilot's own off, and stays off until the cable returns.
    Rig pressed;
    store_power_switch(pressed, power::Switch::Cable);
    pressed.platform.battery().external_power = true;
    REQUIRE(pressed.setup() == Status::Ok);
    pressed.run_for(5000);
    pressed.product.shutdown().request(power::ShutdownReason::LongPress, pressed.now_ms);
    pressed.run_for(10000);
    REQUIRE(pressed.product.ready_to_power_off());
    CHECK_FALSE(pressed.product.cable_returned());
}

// A flat cell off the cable parks the Go, and a master switched on meanwhile raises no wake.
TEST_CASE("product: a cable back while the Go parks for a flat cell restarts it instead") {
    Rig rig;
    store_power_switch(rig, power::Switch::Cable);
    rig.platform.battery().external_power = true;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run_for(5000);

    rig.platform.battery().divider_mv = 3100;
    pull_cable_for(rig, 10000);
    REQUIRE(rig.product.shutdown().reason() == power::ShutdownReason::LowBattery);
    rig.platform.battery().external_power = true;
    rig.run_for(10000);
    REQUIRE(rig.product.ready_to_power_off());
    CHECK(rig.product.cable_returned());

    // On the button, a charger plugged in during the park wakes nothing, and the button still does.
    Rig button;
    button.platform.battery().external_power = true;
    REQUIRE(button.setup() == Status::Ok);
    button.run_for(5000);
    button.platform.battery().divider_mv = 3100;
    pull_cable_for(button, 10000);
    REQUIRE(button.product.shutdown().reason() == power::ShutdownReason::LowBattery);
    button.platform.battery().external_power = true;
    button.run_for(10000);
    REQUIRE(button.product.ready_to_power_off());
    CHECK_FALSE(button.product.cable_returned());
}

// A cable pulled while an image uploads must not switch the Go off halfway through the upload.
TEST_CASE("product: in cable mode an update under way holds the countdown until it ends") {
    Rig rig;
    store_power_switch(rig, power::Switch::Cable);
    rig.platform.battery().external_power = true;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run_for(5000);

    open_upload_window(rig);
    REQUIRE(config(rig).upload_allowed());
    pull_cable_for(rig, power::kCableGoneHoldMs + 10000);
    CHECK_FALSE(rig.product.shutdown().going_down());
    CHECK_FALSE(rig.state().power.cable_off_in_ms);

    rig.tap();
    rig.run_for(1000);
    REQUIRE_FALSE(config(rig).upload_allowed());
    pull_cable_for(rig, power::kCableGoneHoldMs - 10000);
    CHECK_FALSE(rig.product.shutdown().going_down());
    REQUIRE(rig.state().power.cable_off_in_ms);

    pull_cable_for(rig, 20000);
    CHECK(rig.product.shutdown().reason() == power::ShutdownReason::CableGone);
}

// An install restarts the Go, so the new image never saw the cable the old one followed.
TEST_CASE("product: in cable mode the boot after an install switches off without the cable") {
    Rig before;
    install_with_the_cable_pulled(before);

    Rebooted after(before, kStaged, /*confirmed=*/false);
    after.rig.platform.system_power().causes = power::ResetCause::Software;
    REQUIRE(after.rig.setup() == Status::Ok);
    REQUIRE(after.rig.product.power_switch() == power::Switch::Cable);
    pull_cable_for(after.rig, power::kCableGoneHoldMs - 10000);
    CHECK_FALSE(after.rig.product.shutdown().going_down());

    pull_cable_for(after.rig, 20000);
    CHECK(after.rig.product.shutdown().reason() == power::ShutdownReason::CableGone);
}

// A new image that fails its self test serves nothing, and would otherwise drain the cell.
TEST_CASE("product: in cable mode a boot after an install that cannot fly switches off too") {
    Rig before;
    install_with_the_cable_pulled(before);

    constexpr ports::Capabilities kNoGnss = static_cast<ports::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(ports::Capability::Gnss));
    Rebooted after(before, kStaged, /*confirmed=*/false, std::nullopt, kNoGnss);
    after.rig.platform.system_power().causes = power::ResetCause::Software;
    REQUIRE(after.rig.setup() == Status::Down);
    REQUIRE_FALSE(after.rig.product.flyable());
    pull_cable_for(after.rig, power::kCableGoneHoldMs + 10000);
    CHECK(after.rig.product.shutdown().reason() == power::ShutdownReason::CableGone);
}

TEST_CASE("product: a cable back on the ground stops the countdown the ring reads") {
    Rig rig;
    store_power_switch(rig, power::Switch::Cable);
    rig.platform.battery().external_power = true;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run_for(5000);

    // 90 s out leaves about 3.5 minutes, which the ring rounds up to 4.
    pull_cable_for(rig, 90000);
    REQUIRE(rig.state().power.cable_off_in_ms);
    CHECK(power::off_in_min(*rig.state().power.cable_off_in_ms) == 4);
    rig.platform.battery().external_power = true;
    rig.run_for(5000);
    CHECK_FALSE(rig.state().power.cable_off_in_ms);
    CHECK_FALSE(rig.product.shutdown().going_down());
}
