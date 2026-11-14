// A paired Go proposes the transponder its Bravo hears as the aircraft's own, on the ground only.
#include <cstdint>
#include <string>

#include "doctest/doctest.h"
#include "products/skyblip_bravo/own_transponder.h"
#include "products/skyblip_go/pages/aircraft.h"
#include "products/skyblip_go/pages/menu.h"
#include "test/support/adsb_frames.h"
#include "test/support/bravo_flight.h"
#include "test/support/glass_text.h"
#include "test/support/log_transfer.h"
#include "test/support/menu_rig.h"
#include "test/support/tango.h"

using namespace skyblip;

namespace {

constexpr uint32_t kHeardAddress = 0x3A4B5C;
constexpr uint32_t kOtherAddress = 0x3A0001;

std::string heard_fields(uint32_t address, int category = 4) {
    return "\"own_transponder\":" + std::to_string(address) +
           ",\"own_transponder_callsign\":\"FJABC\",\"own_transponder_category\":" +
           std::to_string(category);
}

// What Bravo makes of its own transponder's identification, A7 rotorcraft among them.
int own_category_heard(uint8_t type_code, uint8_t emitter) {
    bravo::OwnTransponder own{};
    adsb::Message identification{};
    REQUIRE(adsb_hear(adsb_identification(kHeardAddress, 0, type_code, emitter, "FJABC"),
                      identification) == Status::Ok);
    identification.reception.rssi_dbm = -12;
    identification.reception.rssi_valid = true;
    uint32_t now_ms = 1000;
    own.hear(identification, now_ms);
    for (uint32_t spent = 0; spent <= 2 * bravo::OwnTransponder::kLeadHoldMs; spent += 200) {
        adsb::Message position{};
        REQUIRE(adsb_hear(adsb_airborne_position(kHeardAddress, 0, 48.5, 8.5, false, 1500),
                          position) == Status::Ok);
        position.reception.rssi_dbm = -12;
        position.reception.rssi_valid = true;
        own.hear(position, now_ms);
        own.tick(now_ms);
        now_ms += 200;
    }
    REQUIRE(own.heard().has_value());
    return own.heard()->aircraft_category;
}

bool glass_reads(Rig& rig, const char* text, int scale) {
    return reads_in(rig.product.screen().framebuffer(), text, 0, 0, go::kGlassWidth,
                    go::kGlassHeight, scale);
}

// Parked with a fix, and Bravo holding an own ICAO so the own-ICAO notice stands aside.
void paired_on_the_radar(Tango& tango, bool fixed = true) {
    tango.bravo.health.own_icao_set = true;
    tango.pair_and_store();
    if (fixed) {
        tango.go.seconds(tango.now_ms, 5, 0, 500);
        REQUIRE(tango.go.state().flight.confirmed_state == flight::FlightState::Ground);
    }
    tango.go.long_touch(tango.now_ms);
    REQUIRE(tango.go.product.screen().page() == go::Page::Radar);
    REQUIRE_FALSE(tango.go.product.screen().noticing());
}

}  // namespace

TEST_CASE("aircraft detection: a transponder Bravo hears is proposed once, and a press stores it") {
    Tango tango;
    paired_on_the_radar(tango);
    tango.bravo.health.own_transponder_fields = heard_fields(kHeardAddress);
    tango.run_for(2500);
    REQUIRE(tango.go.product.screen().noticing());
    CHECK(tango.go.product.screen().notice() == go::ScreenService::Notice::NewAircraft);
    tango.go.run(tango.now_ms, tango.now_ms + 4000);
    tango.now_ms += 4000;
    CHECK(glass_reads(tango.go, "F-JABC", go::kCallsignScale));

    tango.go.press(tango.now_ms);
    tango.run_for(500);
    CHECK_FALSE(tango.go.product.screen().noticing());
    CHECK(tango.go.settings().own_icao == kHeardAddress);
    CHECK(std::string(tango.go.settings().callsign) == "F-JABC");
    CHECK(tango.go.settings().aircraft_type == 4);

    tango.run_for(1500);
    REQUIRE_FALSE(tango.bravo.claims.empty());
    CHECK(field(tango.bravo.claims.back(), "own_icao") == std::to_string(kHeardAddress));

    // Once a boot: another transponder later is the AIRCRAFT menu's to propose, not a notice.
    tango.bravo.health.own_transponder_fields = heard_fields(kOtherAddress);
    tango.run_for(3000);
    CHECK_FALSE(tango.go.product.screen().noticing());
}

TEST_CASE("aircraft detection: a pad on the notice closes it and stores nothing") {
    Tango tango;
    paired_on_the_radar(tango);
    tango.bravo.health.own_transponder_fields = heard_fields(kHeardAddress);
    tango.run_for(2500);
    REQUIRE(tango.go.product.screen().noticing());
    tango.go.tap(tango.now_ms);
    CHECK_FALSE(tango.go.product.screen().noticing());
    CHECK(tango.go.settings().own_icao == go::kOwnIcaoUnset);
}

TEST_CASE("aircraft detection: the transponder the Go already holds raises no notice") {
    Tango tango;
    tango.go.settings().own_icao = kHeardAddress;
    paired_on_the_radar(tango);
    tango.bravo.health.own_transponder_fields = heard_fields(kHeardAddress);
    tango.run_for(3000);
    CHECK_FALSE(tango.go.product.screen().noticing());
}

TEST_CASE("aircraft detection: the AIRCRAFT menu reads what Bravo hears, and a press stores it") {
    Tango tango;
    paired_on_the_radar(tango);
    tango.bravo.health.own_transponder_fields = heard_fields(kHeardAddress);
    tango.run_for(2500);
    tango.go.tap(tango.now_ms);
    REQUIRE_FALSE(tango.go.product.screen().noticing());

    uint32_t& now_ms = tango.now_ms;
    open_menu(tango.go, now_ms);
    focus_on(tango.go, now_ms, go::MenuRow::Detected);
    tango.go.run(now_ms, now_ms + 4000);
    now_ms += 4000;
    CHECK(reads_in(tango.go.product.screen().framebuffer(), "ICAO 3A4B5C", 0, 0, go::kGlassWidth,
                   go::kGlassHeight, 2, false));

    change(tango.go, now_ms);
    CHECK(tango.go.settings().own_icao == kHeardAddress);
    CHECK(tango.go.product.screen().editor().focus() == go::MenuRow::Aircraft);
    run_past_the_write_settle(tango.go, now_ms);
    go::Settings stored{};
    REQUIRE(stored_settings(tango.go, stored));
    CHECK(stored.own_icao == kHeardAddress);
    CHECK(std::string(stored.callsign) == "F-JABC");
}

// A wingman 10 m off in the air is as loud as our own transponder, so the air proposes nothing.
TEST_CASE("aircraft detection: in flight nothing is proposed, and the menu says it waits") {
    Flight flight;
    flight.fly(30);
    REQUIRE(flight.state().flight.confirmed_state == flight::FlightState::Airborne);
    flight.tango.bravo.health.own_transponder_fields = heard_fields(kHeardAddress);
    flight.fly(3);
    CHECK_FALSE(flight.go().product.screen().noticing());
    CHECK_FALSE(flight.state().own_transponder);

    uint32_t& now_ms = flight.tango.now_ms;
    open_menu(flight.go(), now_ms);
    focus_on(flight.go(), now_ms, go::MenuRow::Detected);
    flight.go().run(now_ms, now_ms + 4000);
    now_ms += 4000;
    CHECK(reads_in(flight.glass(), "IN FLIGHT", 0, 0, go::kGlassWidth, go::kGlassHeight,
                   go::kMenuScale, false));
    change(flight.go(), now_ms);
    CHECK(flight.go().settings().own_icao == go::kOwnIcaoUnset);
}

// A Go switched on in the air has no fix yet, and its loudest neighbour may be a wingman.
TEST_CASE("aircraft detection: a Go with no fix proposes nothing, and the menu says why") {
    Tango tango;
    paired_on_the_radar(tango, false);
    REQUIRE(tango.go.state().flight.confirmed_state == flight::FlightState::Unknown);
    tango.bravo.health.own_transponder_fields = heard_fields(kHeardAddress);
    tango.run_for(3000);
    CHECK_FALSE(tango.go.product.screen().noticing());

    uint32_t& now_ms = tango.now_ms;
    open_menu(tango.go, now_ms);
    focus_on(tango.go, now_ms, go::MenuRow::Detected);
    tango.go.run(now_ms, now_ms + 4000);
    now_ms += 4000;
    CHECK(reads_in(tango.go.product.screen().framebuffer(), "NO FIX", 0, 0, go::kGlassWidth,
                   go::kGlassHeight, go::kMenuScale, false));
    change(tango.go, now_ms);
    CHECK(tango.go.settings().own_icao == go::kOwnIcaoUnset);
}

TEST_CASE("aircraft detection: a press before the notice is read stores nothing") {
    Tango tango;
    paired_on_the_radar(tango);
    tango.bravo.health.own_transponder_fields = heard_fields(kHeardAddress);
    for (int i = 0; i < 60 && !tango.go.product.screen().noticing(); i++) tango.run_for(50);
    REQUIRE(tango.go.product.screen().noticing());
    tango.go.press(tango.now_ms);
    CHECK(tango.go.settings().own_icao == go::kOwnIcaoUnset);
    CHECK(tango.go.product.screen().noticing());
}

TEST_CASE("aircraft detection: the notice goes when Bravo stops hearing what it showed") {
    Tango tango;
    paired_on_the_radar(tango);
    tango.bravo.health.own_transponder_fields = heard_fields(kHeardAddress);
    tango.run_for(2500);
    REQUIRE(tango.go.product.screen().noticing());
    tango.bravo.health.own_transponder_fields.clear();
    tango.run_for(1500);
    CHECK_FALSE(tango.go.product.screen().noticing());
    CHECK(tango.go.settings().own_icao == go::kOwnIcaoUnset);
}

// Traffic reads ADS-B A7 as heavy rotorcraft, and a pilot's own aircraft is never assumed heavy.
TEST_CASE("aircraft detection: an A7 transponder proposes a light rotorcraft, never a heavy one") {
    const int category = own_category_heard(4, 7);
    CHECK(category == 3);
    CHECK(own_category_heard(4, 6) == 1);
    CHECK(own_category_heard(4, 3) == 2);

    Tango tango;
    paired_on_the_radar(tango);
    tango.bravo.health.own_transponder_fields = heard_fields(kHeardAddress, category);
    tango.run_for(2500);
    REQUIRE(tango.go.product.screen().noticing());
    tango.go.press(tango.now_ms);
    tango.run_for(500);
    CHECK(tango.go.settings().aircraft_type == 3);
}
