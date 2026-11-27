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
    CHECK_FALSE(ports::has(rig.product.capabilities(), ports::Capability::DieTemperature));
    CHECK(rig.platform.radio().listening_for_adsb());
    rig.run_for(1000);
    CHECK(rig.platform.link().advertised == "skyBlip Bravo 3F2A");
    CHECK(rig.lamp() == indication::Condition::NoFix);
}

// Meshtastic drives the X1's motor as an LRA, and our ERM drive gave the first unit a weak buzz.
TEST_CASE("bravo: the X1's motor is an LRA in closed loop, never buzzed by a calibration at boot") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    models::Drv2605& chip = rig.platform.haptic();
    CHECK(chip.lra_closed_loop());
    CHECK(chip.calibrations == 0);
    CHECK(chip.standby());

    rig.product.board().roles().annunciator.vibrate();
    CHECK(int(chip.drive()) == 0x7F);
    rig.product.board().roles().annunciator.still();
    CHECK_FALSE(chip.moving());
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

// Bravo has no glass, so a browser it trusts is what spares the pilot a blind press.
TEST_CASE("bravo: a unit with no key still draws a browser challenge from its RNG") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.send("{\"command\":\"browser_challenge\"}");
    rig.run_for(200);
    const std::string reply = rig.last_on(events::Endpoint::Config);
    CHECK(reply.find("\"command\":\"browser_challenge\"") != std::string::npos);
    CHECK(reply.find("keyless") == std::string::npos);
}

TEST_CASE("bravo: each boot draws its peer boot epoch from the RNG") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    CHECK(rig.platform.random().draws > 0);
    CHECK(rig.product.peer().boot_epoch() != 0);
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

// A capture tells a blind sky from a slow first fix only if the receiver says what it sees.
TEST_CASE("bravo: the GNSS sends its satellites in view until the first fix, and not after") {
    BravoRig rig;
    rig.gnss_on_air = true;
    rig.platform.gnss().fix_valid = false;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run_for(30000);
    REQUIRE(rig.product.board().gnss().configured());
    CHECK(rig.platform.gnss().sentence_on(models::Ag3335::kGsv));
    CHECK(rig.state().gnss.sky.count() == rig.platform.gnss().satellites_in_view);

    rig.platform.gnss().fix_valid = true;
    rig.run_for(5000);
    REQUIRE(rig.state().own.fix_valid);
    CHECK_FALSE(rig.platform.gnss().sentence_on(models::Ag3335::kGsv));
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
    "bravo: a unit that refuses to fly blinks the critical row, and a hold still switches it off" *
    doctest::test_suite("main")) {
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
