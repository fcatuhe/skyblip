// One verdict on a paired Bravo, from its health: the radar, nearby and status agree.
#include <string>

#include "core/comms/peer.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/pages/nearby.h"
#include "test/support/bravo_flight.h"
#include "test/support/glass_text.h"
#include "test/support/radar_rig.h"

using namespace skyblip;

namespace {

constexpr int kBravoY = go::kNearbyUnitY + go::kNearbyLineHeight;

const ui::Canvas& nearby(Flight& flight) {
    flight.go().tap();
    REQUIRE(flight.go().product.screen().page() == go::Page::Nearby);
    flight.go().run(flight.tango.now_ms, flight.tango.now_ms + 1100);
    return flight.glass();
}

bool row_reads(const ui::Canvas& glass, const char* text, int scale = go::kNearbyScale) {
    return reads_in(glass, text, 0, kBravoY, go::kGlassWidth, kBravoY + go::kNearbyLineHeight,
                    scale);
}

bool status_says(Flight& flight, const char* bravo) {
    return last_status(flight.go()).find(std::string("\"bravo\":\"") + bravo + "\"") !=
           std::string::npos;
}

}  // namespace

// The radar once said MOVE OUT, and walking a Bravo with a dead receiver outdoors fixes nothing.
TEST_CASE(
    "bravo verdict: a Bravo with no UTC and a silent receiver reads GPS OFF on the radar, "
    "on nearby and in status" *
    doctest::test_suite("main")) {
    Flight flight;
    flight.tango.bravo.health.utc_valid = false;
    flight.tango.bravo.health.status_fields =
        "\"charging\":false,\"power_level\":\"normal\",\"gnss_stage\":\"silent\"";
    flight.fly(3);
    CHECK(go::corner_reads(flight.glass(), "GPS OFF", false));
    CHECK_FALSE(go::corner_reads(flight.glass(), "MOVE OUT", false));
    CHECK(status_says(flight, "gps_off"));
    CHECK(row_reads(nearby(flight), "GPS OFF"));
}

// Bravo places its ADS-B on the Go's position, so a Bravo with no fix of its own still relays it.
TEST_CASE(
    "bravo verdict: a live Bravo still finding its fix keeps its B, and nearby counts its "
    "traffic") {
    Flight flight(named_target_every_second(2, 40));
    flight.tango.bravo.health.export_reference = "go";
    flight.tango.bravo.health.status_fields =
        "\"charging\":false,\"power_level\":\"normal\",\"gnss_stage\":\"solving\"";
    flight.fly(3);
    CHECK(flight.state().bravo == comms::BravoState::Live);
    CHECK(go::marks_bravo_live(flight.glass()));
    CHECK_FALSE(go::corner_reads(flight.glass(), "BRAVO"));
    const ui::Canvas& glass = nearby(flight);
    CHECK_FALSE(row_reads(glass, "FINDS FIX"));
    CHECK(row_reads(glass, "TFC", 1));
}

// With no position to place it on, its own or the Go's, Bravo exports nothing, so a B would lie.
TEST_CASE(
    "bravo verdict: a Bravo with UTC and no position to place its ADS-B on says FINDS FIX, "
    "with no B and no tone") {
    Flight flight;
    flight.fly(3);
    REQUIRE(flight.state().bravo == comms::BravoState::Live);
    flight.tones.clear();
    flight.tango.bravo.health.export_reference = "none";
    flight.tango.bravo.health.status_fields =
        "\"charging\":false,\"power_level\":\"normal\",\"gnss_stage\":\"solving\"";
    flight.fly(2);
    CHECK_FALSE(go::marks_bravo_live(flight.glass()));
    CHECK(go::corner_reads(flight.glass(), "BRAVO"));
    CHECK(go::corner_reads(flight.glass(), "FINDS FIX"));
    CHECK(flight.tones.empty());
    CHECK(status_says(flight, "finds_fix"));
    CHECK(row_reads(nearby(flight), "FINDS FIX"));
}

// A Bravo in a bag shows its red lamp to nobody, whatever else is wrong with it.
TEST_CASE("bravo verdict: a lost Bravo on a low cell says its cell over why it is lost") {
    Flight flight;
    flight.tango.bravo.health.utc_valid = false;
    flight.tango.bravo.health.status_fields =
        "\"battery_percent\":9,\"charging\":false,\"power_level\":\"low\",\"gnss_stage\":\"blind\"";
    flight.fly(3);
    CHECK(flight.state().bravo == comms::BravoState::NoUtc);
    CHECK(go::corner_reads(flight.glass(), "BRAVO", false));
    CHECK(go::corner_reads(flight.glass(), "BAT 9%", false));
    CHECK(go::corner_reads(flight.glass(), "MOVE OUT", false));
}

TEST_CASE("bravo verdict: a live Bravo on a low cell keeps its B, and status says cell_low") {
    Flight flight;
    flight.tango.bravo.health.status_fields =
        "\"battery_percent\":9,\"charging\":false,\"power_level\":\"low\"";
    flight.fly(3);
    CHECK(go::marks_bravo_live(flight.glass()));
    CHECK(go::corner_reads(flight.glass(), "BAT 9%"));
    CHECK(status_says(flight, "cell_low"));
}
