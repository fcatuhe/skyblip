// Cable mode: a Go wired to the aircraft's USB socket switches on and off with the master switch.
#include "core/power/cable.h"
#include "core/power/shutdown.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/settings.h"
#include "test/support/product_rig.h"

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

}  // namespace

TEST_CASE("product: in cable mode a cable gone for the hold switches the Go off for the cable" *
          doctest::test_suite("main")) {
    Rig rig;
    store_power_switch(rig, power::Switch::Cable);
    rig.platform.battery().external_power = true;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run_for(5000);

    pull_cable_for(rig, power::kCableGoneHoldMs - 5000);
    CHECK_FALSE(rig.product.shutdown().going_down());

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
