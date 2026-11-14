// ADS-L F.2.3 on the glass: the OWN ICAO field refuses an address Bravo's transponder contradicts.
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

void type_own_icao(Rig& rig, uint32_t& now_ms, const char* wanted) {
    for (int at = 0; at < go::kOwnIcaoCharacters; at++) {
        while (rig.product.screen().editor().text()[at] != wanted[at]) move(rig, now_ms);
        change(rig, now_ms);
    }
}

bool glass_reads(Rig& rig, uint32_t& now_ms, const char* text) {
    rig.run(now_ms, now_ms + 4000);
    now_ms += 4000;
    return reads_in(rig.product.screen().framebuffer(), text, 0, 0, go::kGlassWidth,
                    go::kGlassHeight, 1, true);
}

bool glass_shows(Rig& rig, uint32_t& now_ms, const go::Glass& expected) {
    rig.run(now_ms, now_ms + 4000);
    now_ms += 4000;
    return std::memcmp(rig.product.screen().framebuffer().data(), expected.data(),
                       go::Glass::kBytes) == 0;
}

}  // namespace

TEST_CASE("menu: an own ICAO typed against the transponder Bravo names stays in the field") {
    Tango tango;
    tango.bravo.health.own_icao_set = true;
    tango.pair_and_store();
    tango.go.seconds(tango.now_ms, 5, 0, 500);
    REQUIRE(tango.go.state().flight.confirmed_state == flight::FlightState::Ground);
    tango.go.long_touch(tango.now_ms);
    tango.bravo.health.own_transponder_fields =
        "\"own_transponder\":" + std::to_string(kHeardAddress);
    tango.run_for(2500);
    REQUIRE(tango.go.state().own_transponder == std::optional<uint32_t>(kHeardAddress));
    if (tango.go.product.screen().noticing()) tango.go.tap(tango.now_ms);

    uint32_t& now_ms = tango.now_ms;
    open_menu(tango.go, now_ms);
    focus_on(tango.go, now_ms, go::MenuRow::OwnIcao);
    change(tango.go, now_ms);
    REQUIRE(tango.go.product.screen().editor().editing());
    CHECK(glass_reads(tango.go, now_ms, "BRAVO HEARS 3A4B5C"));

    type_own_icao(tango.go, now_ms, "3A0001");
    CHECK(tango.go.product.screen().editor().editing());
    CHECK(tango.go.product.screen().editor().refused());
    CHECK(tango.go.product.screen().editor().cursor() == 0);
    CHECK(glass_reads(tango.go, now_ms, "REFUSED: BRAVO HEARS 3A4B5C"));
    go::CallsignSnapshot field;
    field.title = "OWN ICAO";
    field.text = "3A0001";
    field.own_transponder = kHeardAddress;
    field.refused = true;
    go::Glass expected;
    go::draw_callsign(expected, field);
    CHECK(glass_shows(tango.go, now_ms, expected));
    run_past_the_write_settle(tango.go, now_ms);
    CHECK(tango.go.settings().own_icao == go::kOwnIcaoUnset);

    type_own_icao(tango.go, now_ms, "3A4B5C");
    CHECK_FALSE(tango.go.product.screen().editor().editing());
    run_past_the_write_settle(tango.go, now_ms);
    CHECK(tango.go.settings().own_icao == kHeardAddress);
}

// ICAO Annex 10 Volume III, Appendix to Chapter 9, 5.2 f): 24 ones is assigned to no aircraft.
TEST_CASE("menu: an own ICAO of FFFFFF typed on the glass stores nothing") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 100;
    open_menu(rig, now_ms);
    focus_on(rig, now_ms, go::MenuRow::OwnIcao);
    change(rig, now_ms);
    REQUIRE(rig.product.screen().editor().editing());
    CHECK_FALSE(glass_reads(rig, now_ms, "BRAVO HEARS"));

    type_own_icao(rig, now_ms, "FFFFFF");
    CHECK_FALSE(rig.product.screen().editor().editing());
    run_past_the_write_settle(rig, now_ms);
    CHECK(rig.settings().own_icao == go::kOwnIcaoUnset);
}
