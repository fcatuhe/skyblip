// ADS-L F.2.3 on the glass: the ADS-B field refuses an address Bravo's transponder contradicts.
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

#include "doctest/doctest.h"
#include "products/skyblip_go/pages/menu.h"
#include "test/support/glass_text.h"
#include "test/support/menu_rig.h"
#include "test/support/tango.h"

using namespace skyblip;

namespace {

constexpr uint32_t kHeardAddress = 0x3A4B5C;

void type_aircraft_address(Rig& rig, const char* wanted) {
    for (int at = 0; at < go::kAircraftAddressCharacters; at++) {
        while (rig.product.screen().editor().text()[at] != wanted[at]) move(rig);
        change(rig);
    }
}

bool glass_reads(Rig& rig, const char* text) {
    rig.run_for(4000);
    return reads_in(rig.product.screen().framebuffer(), text, 0, 0, go::kGlassWidth,
                    go::kGlassHeight, go::kMenuScale, true);
}

bool glass_shows(Rig& rig, const go::Glass& expected) {
    rig.run_for(4000);
    return std::memcmp(rig.product.screen().framebuffer().data(), expected.data(),
                       go::Glass::kBytes) == 0;
}

}  // namespace

TEST_CASE("menu: an aircraft address typed against the transponder Bravo names stays in the field" *
          doctest::test_suite("main")) {
    Tango tango;
    tango.bravo.health.aircraft_address_set = true;
    tango.pair_and_store();
    tango.go.seconds(5, 0, 500);
    REQUIRE(tango.go.state().flight.confirmed_state == flight::FlightState::Ground);
    tango.go.long_touch();
    tango.bravo.health.own_transponder_fields =
        "\"own_transponder\":" + std::to_string(kHeardAddress);
    tango.run_for(2500);
    REQUIRE(comms::address_of(tango.go.state().own_transponder) == kHeardAddress);
    if (tango.go.product.screen().noticing()) tango.go.tap();

    open_menu(tango.go);
    focus_on(tango.go, go::MenuRow::AircraftAddress);
    change(tango.go);
    REQUIRE(tango.go.product.screen().editor().editing());
    CHECK(glass_reads(tango.go, "BRAVO HEARS"));
    CHECK(glass_reads(tango.go, "3A4B5C"));
    CHECK_FALSE(glass_reads(tango.go, go::kOwnTransponderRefusedText));

    type_aircraft_address(tango.go, "3A0001");
    CHECK(tango.go.product.screen().editor().editing());
    CHECK(tango.go.product.screen().editor().refused());
    CHECK(tango.go.product.screen().editor().cursor() == 0);
    CHECK(glass_reads(tango.go, go::kOwnTransponderRefusedText));
    CHECK(glass_reads(tango.go, "BRAVO HEARS"));
    go::CallsignSnapshot field;
    field.title = "ADS-B";
    field.text = "3A0001";
    field.heard = kHeardAddress;
    field.heard_text = go::kOwnTransponderText;
    field.refused = true;
    go::Glass expected;
    go::draw_callsign(expected, field);
    CHECK(glass_shows(tango.go, expected));
    run_past_the_write_settle(tango.go);
    CHECK(tango.go.settings().aircraft_address == go::kAircraftAddressUnset);

    type_aircraft_address(tango.go, "3A4B5C");
    CHECK_FALSE(tango.go.product.screen().editor().editing());
    run_past_the_write_settle(tango.go);
    CHECK(tango.go.settings().aircraft_address == kHeardAddress);
}

// ICAO Annex 10 Volume III, Appendix to Chapter 9, 5.2 f): 24 ones is assigned to no aircraft.
TEST_CASE("menu: an aircraft address of FFFFFF typed on the glass stores nothing") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.now_ms = 100;
    open_menu(rig);
    focus_on(rig, go::MenuRow::AircraftAddress);
    change(rig);
    REQUIRE(rig.product.screen().editor().editing());
    CHECK_FALSE(glass_reads(rig, "BRAVO HEARS"));

    type_aircraft_address(rig, "FFFFFF");
    CHECK_FALSE(rig.product.screen().editor().editing());
    run_past_the_write_settle(rig);
    CHECK(rig.settings().aircraft_address == go::kAircraftAddressUnset);
}
