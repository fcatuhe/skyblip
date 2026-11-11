// Bravo has no screen: a phone's prompt is a lamp pattern, and two presses answer it.
#include <string>

#include "doctest/doctest.h"
#include "test/support/bravo_rig.h"

using namespace skyblip;

namespace {

constexpr const char* kAccepted = "{\"command\":\"acknowledgement\",\"acknowledge\":true}";

bool refused_with(const std::string& frame, const char* reason) {
    return frame.find("\"acknowledge\":false") != std::string::npos &&
           frame.find(reason) != std::string::npos;
}

void hold_through_power_off(BravoRig& rig) {
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(2);
    rig.platform.set_button_down(true);
    rig.run_for(100);
    rig.send("{\"command\":\"power_off\"}");
    rig.run_for(1000);
    rig.platform.set_button_down(false);
}

uint32_t arming_pass_ms() {
    BravoRig dry;
    hold_through_power_off(dry);
    for (int pass = 0; pass < 200 && !dry.product.button().armed(); pass++) dry.run_for(10);
    REQUIRE(dry.product.button().armed());
    return dry.now_ms - 10;
}

}  // namespace

TEST_CASE("bravo: a set from a phone blinks the prompt and two presses store it") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(2);
    rig.send("{\"command\":\"set\",\"alarm_enabled\":false}");
    rig.run_for(1000);
    REQUIRE(rig.product.button().prompt() == comms::Pending::Set);
    CHECK(rig.lamp() == indication::Condition::Prompt);
    CHECK(rig.product.settings().alarm_enabled);

    rig.double_press();
    rig.run_for(200);
    CHECK(rig.last_on(events::Endpoint::Config) == kAccepted);
    CHECK_FALSE(rig.product.settings().alarm_enabled);
    CHECK(rig.lamp() == indication::Condition::AliveLinked);
}

TEST_CASE("bravo: one press refuses a prompt, and nothing changes") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(2);
    rig.send("{\"command\":\"set\",\"own_icao\":3958150}");
    rig.run_for(1000);
    REQUIRE(rig.product.button().armed());

    rig.press();
    rig.run_for(1000);
    CHECK(refused_with(rig.last_on(events::Endpoint::Config), "cancelled"));
    CHECK(rig.product.settings().own_icao == bravo::kOwnIcaoUnset);
    CHECK(rig.product.button().prompt() == comms::Pending::None);
}

TEST_CASE("bravo: presses made before the prompt stood are never spent on it") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(2);
    rig.send("{\"command\":\"power_off\"}");
    rig.step();
    rig.double_press();
    CHECK_FALSE(rig.product.shutdown().going_down());
    CHECK(rig.product.button().prompt() == comms::Pending::PowerOff);
}

// The Go refuses a prompt in flight only, so a Bravo with no fix since boot can still be updated.
TEST_CASE("bravo: with no fix since boot a power-off from the phone stands, and two presses obey") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_blind(2);
    REQUIRE(rig.state().flight.confirmed_state == flight::FlightState::Unknown);
    rig.send("{\"command\":\"power_off\"}");
    rig.run_for(1000);
    REQUIRE(rig.lamp() == indication::Condition::Prompt);

    rig.double_press();
    CHECK(rig.product.shutdown().going_down());
    CHECK(rig.product.shutdown().reason() == power::ShutdownReason::LinkRequest);
}

TEST_CASE("bravo: in flight a prompt is refused before it stands") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(20, 25000);
    REQUIRE(rig.state().flight.confirmed_state == flight::FlightState::Airborne);
    rig.send("{\"command\":\"recovery\"}");
    rig.run_for(200);
    CHECK(refused_with(rig.last_on(events::Endpoint::Config), "in_flight"));
    CHECK(rig.product.button().prompt() == comms::Pending::None);
}

TEST_CASE("bravo: in flight an advisory flickers the lamp and sounds, and one press dismisses it") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(20, 25000);
    REQUIRE(rig.state().flight.confirmed_state == flight::FlightState::Airborne);
    rig.hear_aircraft(0x3C6586, 800);
    rig.seconds_with_fix(1, 25000);
    REQUIRE(rig.state().alarm_undismissed == traffic::Level::Advisory);
    CHECK(rig.lamp() == indication::Condition::Alarm);
    CHECK(rig.platform.annunciator().tone_commands() > 0);

    rig.press();
    CHECK(rig.state().alarm_undismissed == traffic::Level::None);
    CHECK(rig.state().alarm_level == traffic::Level::Advisory);
    CHECK(rig.lamp() == indication::Condition::Alive);
}

// GPT's review: a press begun before the prompt stood was released into an armed gesture.
TEST_CASE("bravo: a press held through a prompt's arrival, and one press after, allow nothing") {
    const char* const prompts[] = {
        "{\"command\":\"set\",\"alarm_enabled\":false}",
        "{\"command\":\"dfu\",\"version\":\"0.2.0+15\"}",
        "{\"command\":\"recovery\"}",
        "{\"command\":\"power_off\"}",
        "{\"command\":\"gnss_cold_start\"}",
    };
    for (const char* prompt : prompts) {
        CAPTURE(prompt);
        BravoRig rig;
        REQUIRE(rig.setup() == Status::Ok);
        rig.seconds_with_fix(2);
        rig.platform.set_button_down(true);
        rig.run_for(100);
        rig.send(prompt);
        rig.run_for(1000);
        REQUIRE(rig.product.button().answering());
        CHECK_FALSE(rig.product.button().armed());

        rig.platform.set_button_down(false);
        rig.run_for(100);
        rig.press();
        rig.run_for(2000);
        CHECK(rig.product.button().answering());
        CHECK_FALSE(rig.product.config_link().config().receiving_firmware());
        CHECK_FALSE(rig.product.shutdown().going_down());
        CHECK(rig.product.settings().alarm_enabled);
        CHECK(rig.last_on(events::Endpoint::Config).find("\"acknowledge\":true") ==
              std::string::npos);
    }
}

TEST_CASE("bravo: the gesture stays disarmed while the button is down, and arms once it settles") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(2);
    rig.platform.set_button_down(true);
    rig.run_for(100);
    rig.send("{\"command\":\"power_off\"}");
    rig.run_for(1500);
    CHECK_FALSE(rig.product.button().armed());
    rig.platform.set_button_down(false);
    rig.run_for(300);
    CHECK_FALSE(rig.product.button().armed());
    rig.run_for(500);
    REQUIRE(rig.product.button().armed());

    rig.double_press();
    CHECK(rig.product.shutdown().reason() == power::ShutdownReason::LinkRequest);
}

// GPT's review of #245: the same kind of prompt staged again before the next pass is a new prompt.
TEST_CASE("bravo: a replaced prompt cannot spend a hold begun on the one it replaced") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(2);
    rig.send("{\"command\":\"set\",\"own_icao\":3958150}");
    rig.run_for(1500);
    REQUIRE(rig.product.button().armed());
    rig.platform.set_button_down(true);
    rig.run_for(100);
    rig.platform.link().drop_link();
    rig.raise_link(2);
    rig.send("{\"command\":\"set\",\"alarm_enabled\":false}");
    rig.run_for(800);
    REQUIRE(rig.product.config_link().config().claim_holder() == 2);
    REQUIRE(rig.product.button().prompt() == comms::Pending::Set);
    rig.platform.set_button_down(false);
    rig.run_for(100);
    rig.press();
    rig.run_for(200);
    CHECK(rig.product.button().prompt() == comms::Pending::Set);
    CHECK(rig.product.settings().alarm_enabled);
}

// GPT's review of #245: the debounce publishes a down a pass or more after it began.
TEST_CASE("bravo: a press that began before the gesture armed is never spent on it") {
    const uint32_t armed_ms = arming_pass_ms();

    BravoRig rig;
    hold_through_power_off(rig);
    rig.run_for(armed_ms - 20 - rig.now_ms);
    rig.platform.set_button_down(true);
    rig.run_for(100);
    CHECK_FALSE(rig.product.button().armed());
    rig.platform.set_button_down(false);
    rig.run_for(100);
    rig.press();
    rig.run_for(200);
    CHECK_FALSE(rig.product.shutdown().going_down());
    CHECK(rig.product.button().prompt() == comms::Pending::PowerOff);
}

// GPT's re-review of #245: the button was never up a whole window before the double press.
TEST_CASE("bravo: after a press that began before arming, a double press waits a quiet window") {
    const uint32_t armed_ms = arming_pass_ms();

    BravoRig rig;
    hold_through_power_off(rig);
    rig.run_for(armed_ms - 20 - rig.now_ms);
    rig.platform.set_button_down(true);
    rig.run_for(100);
    rig.platform.set_button_down(false);
    rig.run_for(50);
    rig.double_press();
    CHECK_FALSE(rig.product.shutdown().going_down());
    CHECK(rig.product.button().prompt() == comms::Pending::PowerOff);

    rig.run_for(700);
    rig.double_press();
    CHECK(rig.product.shutdown().reason() == power::ShutdownReason::LinkRequest);
}
