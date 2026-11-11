// Bravo on the X1's host models: the parts it finds, the name it advertises, and what it refuses.
#include <string>

#include "doctest/doctest.h"
#include "test/support/bravo_rig.h"

using namespace skyblip;

TEST_CASE("bravo: boots on the X1's host models and listens on 1090 MHz under its tag") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    CHECK(rig.product.flyable());
    CHECK(ports::has(rig.product.capabilities(),
                     ports::Capability::AdsbReceiver | ports::Capability::Gnss |
                         ports::Capability::Barometer | ports::Capability::HapticDriver));
    CHECK(rig.product.degraded() == ports::Capability::DieTemperature);
    CHECK(rig.platform.radio().listening_for_adsb());
    rig.run_for(1000);
    CHECK(rig.platform.link().advertised == "skyBlip Bravo 3F2A");
    CHECK(rig.lamp() == indication::Condition::NoFix);
}

TEST_CASE("bravo: the inventory names the barometer, the haptic driver and the RTC it found") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    const ports::Inventory& found = rig.product.board().inventory();
    CHECK(found.barometer_address == parts::Spa06::kAddressPrimary);
    CHECK(found.haptic == ports::HapticKind::WaveformDriver);
    CHECK(found.rtc_fitted);
    CHECK(found.has_i2c_address(parts::Drv2605::kAddress));
}

TEST_CASE("bravo: the name is the tag's whatever activation says, because Bravo has none") {
    char name[identity::kBluetoothNameBytes + 1];
    CHECK(bravo::bluetooth_name(name, sizeof(name), bravo::kProductName,
                                identity::Activation{0x1234, 0x3F2A}) == 18);
    CHECK(std::string(name) == "skyBlip Bravo 3F2A");
}

TEST_CASE("bravo: a unit keeps no key, so an activation request is refused keyless") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.send("{\"command\":\"challenge\"}");
    rig.run_for(200);
    CHECK(rig.last_on(events::Endpoint::Config).find("keyless") != std::string::npos);
    CHECK_FALSE(rig.state().activation.activated());
}

TEST_CASE("bravo: a unit whose receiver does not answer refuses to fly and advertises nothing") {
    BravoRig rig(ports::without(platform::host::MeshtrackerX1::kFullyFitted,
                                ports::Capability::AdsbReceiver));
    CHECK(rig.setup() == Status::Down);
    CHECK_FALSE(rig.product.flyable());
    rig.run_for(1000);
    CHECK(rig.platform.link().advertise_calls == 0);
}

TEST_CASE("bravo: the AG3335 model is configured and its fixes reach own-ship") {
    BravoRig rig;
    rig.gnss_on_air = true;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run_for(30000);
    CHECK(rig.product.board().gnss().configured());
    CHECK(rig.state().own.fix_valid);
    CHECK(rig.lamp() == indication::Condition::Alive);
}

// An image MCUboot is trying must confirm itself on Bravo's hardware, or the next boot reverts it.
TEST_CASE("bravo: a new image confirms itself once the receiver is up and the GNSS answers") {
    BravoRig rig;
    rig.platform.dfu().image_confirmed = false;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run_for(1000);
    CHECK(rig.platform.dfu().confirms == 0);

    rig.seconds_blind(1);
    CHECK(rig.platform.dfu().confirms == 1);
    CHECK(rig.platform.dfu().image_confirmed);
}

// Opus's review: a refused Bravo stayed awake and dark, so it looked switched off.
TEST_CASE(
    "bravo: a unit that refuses to fly blinks the critical row, and a hold still switches it off") {
    BravoRig rig(ports::without(platform::host::MeshtrackerX1::kFullyFitted,
                                ports::Capability::AdsbReceiver));
    REQUIRE(rig.setup() == Status::Down);
    rig.run_for(3000);
    CHECK(rig.platform.indicator().lightings() >= 4);
    CHECK(rig.product.refusal_lamp() == indication::Condition::Critical);

    rig.hold_button(power::kLongPressHoldMs + 200);
    for (int i = 0; i < 1000 && !rig.product.ready_to_power_off(); i++) rig.run_for(10);
    CHECK(rig.product.ready_to_power_off());
    CHECK_FALSE(rig.platform.indicator().lit());
}
