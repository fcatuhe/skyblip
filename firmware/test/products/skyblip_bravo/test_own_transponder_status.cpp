// A page proposes the transponder Bravo hears as the aircraft address, as the Go's menu does.
#include <cstdint>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "test/support/bravo_rig.h"
#include "test/support/log_transfer.h"

using namespace skyblip;

namespace {

// INFO: fc 15nov26 in France's address block, so its FJABC identification reads as F-JABC
constexpr uint32_t kOwn = 0x3A4B5C;
constexpr int8_t kCockpitDbm = -12;
constexpr int32_t kTaxiMmS = 0;
constexpr int32_t kCruiseMmS = 25000;

void cockpit_second(BravoRig& rig, int32_t speed_mm_s) {
    const double latitude = BravoRig::kLatitude1e7 / 1e7;
    const double longitude = BravoRig::kLongitude1e7 / 1e7;
    rig.hear(adsb::identification(kOwn, 0, 4, 1, "FJABC"), kCockpitDbm);
    for (const bool odd : {false, true, false, true})
        rig.hear(adsb::airborne_position(kOwn, 0, latitude, longitude, odd, 1500), kCockpitDbm);
    rig.seconds_with_fix(1, speed_mm_s);
}

void cockpit_hold(BravoRig& rig, int32_t speed_mm_s) {
    for (uint32_t second = 0; second * 1000 <= bravo::OwnTransponder::kLeadHoldMs + 1000; second++)
        cockpit_second(rig, speed_mm_s);
}

std::vector<std::string> statuses(BravoRig& rig) {
    std::vector<std::string> out;
    for (const auto& frame : rig.platform.link().sent) {
        if (frame.endpoint != events::Endpoint::Config) continue;
        const std::string text = link_text(frame.bytes);
        if (field(text, "command") == "status") out.push_back(text);
    }
    return out;
}

}  // namespace

TEST_CASE("bravo: on the ground status names the transponder Bravo hears, pushed unasked") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.raise_link();
    rig.seconds_with_fix(5, kTaxiMmS);
    REQUIRE(rig.state().flight.confirmed_state == flight::FlightState::Ground);
    rig.platform.link().clear();

    cockpit_hold(rig, kTaxiMmS);
    const std::vector<std::string> pushed = statuses(rig);
    REQUIRE_FALSE(pushed.empty());
    CHECK(field(pushed.back(), "own_transponder") == std::to_string(kOwn));
    CHECK(field(pushed.back(), "own_transponder_callsign") == "F-JABC");
    CHECK(field(pushed.back(), "own_transponder_category") == "1");
}

// A wingman 10 m off in the air is as loud as our own transponder, so the air proposes nothing.
TEST_CASE("bravo: in flight status names no transponder, however loud") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.raise_link();
    rig.seconds_with_fix(20, kCruiseMmS);
    REQUIRE(rig.state().flight.confirmed_state == flight::FlightState::Airborne);

    cockpit_hold(rig, kCruiseMmS);
    REQUIRE(rig.product.adsb().own_transponder().heard().has_value());
    rig.send("{\"command\":\"status\"}");
    rig.run_for(200);
    CHECK(field(rig.last_on(events::Endpoint::Config), "command") == "status");
    CHECK(field(rig.last_on(events::Endpoint::Config), "own_transponder").empty());
}

// Most light aircraft carry Mode S with no ADS-B out, so all Bravo hears of its own is replies.
TEST_CASE("bravo: a transponder without ADS-B out is named from its replies alone" *
          doctest::test_suite("main")) {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(5, kTaxiMmS);
    REQUIRE(rig.state().flight.confirmed_state == flight::FlightState::Ground);

    for (uint32_t second = 0; second * 1000 <= bravo::OwnTransponder::kLeadHoldMs + 1000;
         second++) {
        rig.hear_mode_s(adsb::all_call(kOwn, 5), kCockpitDbm);
        rig.hear_mode_s(adsb::altitude_reply(kOwn, 1500), kCockpitDbm);
        rig.seconds_with_fix(1, kTaxiMmS);
    }
    REQUIRE(rig.state().own_transponder.has_value());
    CHECK(rig.state().own_transponder->address == kOwn);
    CHECK(std::string(rig.state().own_transponder->callsign).empty());
}
