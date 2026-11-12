// Bravo has no glass, so a page arms its diagnostics capture over the link and two presses allow
// it.
#include <string>

#include "doctest/doctest.h"
#include "test/support/bravo_rig.h"

using namespace skyblip;

namespace {

constexpr const char* kArmed =
    "{\"command\":\"acknowledgement\",\"acknowledge\":true,\"reason\":\"capture\"}";

std::string refusal(const char* reason) {
    return std::string("{\"command\":\"acknowledgement\",\"acknowledge\":false,\"reason\":\"") +
           reason + "\"}";
}

std::string capture(const char* profile) {
    return std::string("{\"command\":\"capture\",\"profile\":\"") + profile + "\"}";
}

void allow(BravoRig& rig, const char* profile) {
    rig.send(capture(profile).c_str());
    rig.run_for(1000);
    REQUIRE(rig.product.button().armed());
    rig.double_press();
    rig.run_for(200);
}

}  // namespace

TEST_CASE(
    "bravo capture: the command blinks the prompt, and two presses arm the profile it names") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(2);
    rig.send(capture("power_run").c_str());
    rig.run_for(1000);
    CHECK(rig.last_on(events::Endpoint::Config) ==
          "{\"command\":\"acknowledgement\",\"acknowledge\":false,\"pending\":true,"
          "\"reason\":\"confirm_capture\"}");
    CHECK(rig.product.button().prompt() == comms::Pending::Capture);
    CHECK(rig.lamp() == indication::Condition::Prompt);
    CHECK_FALSE(rig.product.capture().capturing());

    rig.double_press();
    rig.run_for(200);
    CHECK(rig.last_on(events::Endpoint::Config) == kArmed);
    CHECK(rig.product.capture().capturing());
    CHECK(rig.product.diagnostics().profile() == diagnostics::Profile::PowerRun);
    CHECK(rig.state().capture.armed);
    CHECK(rig.lamp() == indication::Condition::AliveCapturing);
}

TEST_CASE("bravo capture: two presses on a stop end the capture, and the lamp winks once again") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(2);
    allow(rig, "full");
    REQUIRE(rig.product.capture().capturing());
    REQUIRE(rig.product.diagnostics().profile() == diagnostics::Profile::Full);
    const uint32_t session = rig.product.capture().session_id();

    allow(rig, "stop");
    rig.run_for(1000);
    CHECK(rig.last_on(events::Endpoint::Config) == kArmed);
    CHECK_FALSE(rig.product.capture().capturing());
    CHECK(rig.state().capture.stopped == bus::CaptureStop::Pilot);
    CHECK(rig.product.capture().session_id() == session);
    CHECK(rig.lamp() == indication::Condition::AliveLinked);
}

TEST_CASE("bravo capture: one press refuses it, and nothing is armed") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(2);
    rig.send(capture("flight_run").c_str());
    rig.run_for(1000);
    REQUIRE(rig.product.button().armed());

    rig.press();
    rig.run_for(1000);
    CHECK(rig.last_on(events::Endpoint::Config) == refusal("cancelled"));
    CHECK_FALSE(rig.product.capture().capturing());
    CHECK_FALSE(rig.product.diagnostics().armed());
}

TEST_CASE("bravo capture: an unanswered prompt expires, and nothing is armed") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(2);
    rig.send(capture("full").c_str());
    rig.run_for(comms::kPromptCeilingMs + 1000);
    CHECK(rig.last_on(events::Endpoint::Config) == refusal("expired"));
    CHECK_FALSE(rig.product.capture().capturing());
}

TEST_CASE("bravo capture: in flight it is refused before it stands, and a take-off cancels it") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(2);
    rig.send(capture("full").c_str());
    rig.run_for(1000);
    REQUIRE(rig.product.button().prompt() == comms::Pending::Capture);
    rig.seconds_with_fix(20, 25000);
    REQUIRE(rig.state().flight.confirmed_state == flight::FlightState::Airborne);
    CHECK(rig.product.button().prompt() == comms::Pending::None);
    CHECK_FALSE(rig.product.capture().capturing());

    rig.send(capture("full").c_str());
    rig.run_for(200);
    CHECK(rig.last_on(events::Endpoint::Config) == refusal("in_flight"));
    CHECK(rig.product.button().prompt() == comms::Pending::None);
    CHECK_FALSE(rig.product.capture().capturing());
}

TEST_CASE("bravo capture: an arm while one runs and a stop while none runs are refused at once") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(2);
    rig.send(capture("stop").c_str());
    rig.run_for(200);
    CHECK(rig.last_on(events::Endpoint::Config) == refusal("not_capturing"));
    CHECK(rig.product.button().prompt() == comms::Pending::None);

    allow(rig, "power_run");
    REQUIRE(rig.product.capture().capturing());
    rig.send(capture("full").c_str());
    rig.run_for(200);
    CHECK(rig.last_on(events::Endpoint::Config) == refusal("capturing"));
    CHECK(rig.product.button().prompt() == comms::Pending::None);
    CHECK(rig.product.diagnostics().profile() == diagnostics::Profile::PowerRun);
}

TEST_CASE("bravo capture: a frame with no profile, or one past stop, stages nothing") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(2);
    rig.send("{\"command\":\"capture\"}");
    rig.run_for(200);
    CHECK(rig.last_on(events::Endpoint::Config) == refusal("invalid"));
    rig.send("{\"command\":\"capture\",\"profile\":4}");
    rig.run_for(200);
    CHECK(rig.last_on(events::Endpoint::Config) == refusal("out_of_range"));
    CHECK(rig.product.button().prompt() == comms::Pending::None);
}

TEST_CASE("bravo capture: a unit with no flash refuses it no_storage") {
    BravoRig rig(
        ports::without(platform::host::MeshtrackerX1::kFullyFitted, ports::Capability::Storage));
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(2);
    rig.send(capture("full").c_str());
    rig.run_for(200);
    CHECK(rig.last_on(events::Endpoint::Config) == refusal("no_storage"));
    CHECK(rig.product.button().prompt() == comms::Pending::None);
}
