// The FLARM menu: what the Go hears aboard, and the FLARM ID a pilot types or erases.
#include <string>

#include "doctest/doctest.h"
#include "products/skyblip_go/pages/aircraft.h"
#include "products/skyblip_go/pages/menu.h"
#include "products/skyblip_go/settings.h"
#include "test/support/menu_bench.h"

using namespace skyblip;
using namespace skyblip::go;

namespace {

constexpr uint32_t kOwnFlarm = 0xDD1234;

}  // namespace

TEST_CASE("flarm menu: FLARM opens what the Go hears and the ID, and the pad comes back to it" *
          doctest::test_suite("main")) {
    Bench bench;
    bench.focus_on(MenuRow::FlarmHeard);
    CHECK(bench.editor.flarm());
    CHECK(bench.move() == MenuAction::Moved);
    CHECK(bench.editor.focus() == MenuRow::FlarmId);
    CHECK(bench.move() == MenuAction::Moved);
    CHECK_FALSE(bench.editor.flarm());
    CHECK(bench.editor.focus() == MenuRow::Flarm);
}

TEST_CASE("flarm menu: a press on what the Go hears stores it, and a match stores nothing") {
    Bench bench;
    bench.values.flarm = FlarmDetected{FlarmDetection::Heard, kOwnFlarm};
    bench.focus_on(MenuRow::FlarmHeard);
    CHECK(bench.change() == MenuAction::Changed);
    CHECK(bench.values.settings.flarm_id == kOwnFlarm);
    CHECK(bench.editor.focus() == MenuRow::Flarm);

    bench.focus_on(MenuRow::FlarmHeard);
    CHECK(bench.change() == MenuAction::None);
}

TEST_CASE("flarm menu: an ICAO address or no FLARM heard stores nothing") {
    for (const FlarmDetection state : {FlarmDetection::Icao, FlarmDetection::Other,
                                       FlarmDetection::Listening, FlarmDetection::Airborne}) {
        Bench bench;
        bench.values.flarm = FlarmDetected{state, kOwnFlarm};
        bench.focus_on(MenuRow::FlarmHeard);
        CHECK(bench.change() == MenuAction::None);
        CHECK(bench.values.settings.flarm_id == kFlarmIdUnset);
    }
}

TEST_CASE("flarm menu: six digits typed are the FLARM ID, and the aircraft address stays") {
    Bench bench;
    bench.values.settings.aircraft_address = 0x3A4B5C;
    bench.focus_on(MenuRow::FlarmId);
    REQUIRE(bench.change() == MenuAction::Moved);
    REQUIRE(bench.editor.editing());
    CHECK(bench.type("DD1234") == MenuAction::Changed);
    CHECK(bench.values.settings.flarm_id == kOwnFlarm);
    CHECK(bench.values.settings.aircraft_address == 0x3A4B5Cu);
}

// The Go hearing a FLARM aboard refuses another, so a neighbour is never hidden by a typo.
TEST_CASE("flarm menu: a FLARM ID other than the one the Go hears is refused in the open field") {
    Bench bench;
    bench.values.own_flarm = kOwnFlarm;
    bench.focus_on(MenuRow::FlarmId);
    REQUIRE(bench.change() == MenuAction::Moved);
    bench.type("DD5678");
    CHECK(bench.editor.editing());
    CHECK(bench.editor.refused());
    CHECK(bench.editor.cursor() == 0);
    CHECK(bench.values.settings.flarm_id == kFlarmIdUnset);

    CHECK(bench.type("DD1234") == MenuAction::Changed);
    CHECK(bench.values.settings.flarm_id == kOwnFlarm);
}

TEST_CASE("flarm menu: erasing the FLARM ID asks, and ERASE clears it alone") {
    Bench bench;
    bench.values.settings.flarm_id = kOwnFlarm;
    bench.values.settings.aircraft_address = 0x3A4B5C;
    bench.focus_on(MenuRow::FlarmId);
    REQUIRE(bench.change() == MenuAction::Moved);
    while (bench.editor.text()[0] != kCallsignBlank) bench.move();
    CHECK(bench.change() == MenuAction::Moved);
    REQUIRE(bench.editor.clearing());
    CHECK(std::string(bench.editor.text()) == "DD1234");
    CHECK(bench.change() == MenuAction::Changed);
    CHECK(bench.values.settings.flarm_id == kFlarmIdUnset);
    CHECK(bench.values.settings.aircraft_address == 0x3A4B5Cu);
}

TEST_CASE("flarm menu: the rows read the stored ID, or NONE") {
    MenuValues values = default_menu_values();
    char text[kMenuValueBytes];
    menu_row_value(text, MenuRow::Flarm, values);
    CHECK(std::string(text) == "NONE");
    values.settings.flarm_id = kOwnFlarm;
    menu_row_value(text, MenuRow::FlarmId, values);
    CHECK(std::string(text) == "DD1234");
    CHECK(std::string(menu_row_label(MenuRow::Flarm)) == "FLARM");
    CHECK(std::string(menu_row_label(MenuRow::FlarmId)) == "FLARM ID");
}
