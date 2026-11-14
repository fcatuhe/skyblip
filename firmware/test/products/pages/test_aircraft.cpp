// The AIRCRAFT menu as drawn and as a thumb walks it: what Bravo heard, and the three rows it sets.
#include <cstring>
#include <string>

#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/aircraft.h"
#include "products/skyblip_go/pages/menu.h"
#include "products/skyblip_go/settings.h"
#include "test/support/frame_glass.h"
#include "test/support/glass_text.h"
#include "test/support/menu_bench.h"

using namespace skyblip;
using namespace skyblip::go;

namespace {

constexpr uint32_t kHeardAddress = 0x3A4B5C;
constexpr uint8_t kGlider = 4;

Detected heard_glider() {
    Detected detected;
    detected.state = Detection::Heard;
    detected.address = kHeardAddress;
    std::strcpy(detected.callsign, "F-JABC");
    detected.aircraft_type = kGlider;
    return detected;
}

Glass aircraft_menu_of(const MenuValues& values, MenuRow focus) {
    MenuSnapshot snapshot;
    snapshot.values = values;
    snapshot.focus = focus;
    snapshot.aircraft = true;
    Glass canvas;
    draw_menu(canvas, snapshot);
    return canvas;
}

bool reads(const Glass& canvas, const char* text, int scale, bool ink = true) {
    return reads_in(canvas, text, 0, 0, Glass::kWidth, Glass::kHeight, scale, ink);
}

}  // namespace

TEST_CASE("aircraft: what Bravo heard becomes the own ICAO, the callsign and the type at once") {
    Settings settings = go::defaults();
    CHECK(adopt(heard_glider(), settings));
    CHECK(settings.own_icao == kHeardAddress);
    CHECK(std::string(settings.callsign) == "F-JABC");
    CHECK(settings.aircraft_type == kGlider);
    CHECK(matches(heard_glider(), settings));

    // A second time there is nothing to change, so nothing is stored.
    CHECK_FALSE(adopt(heard_glider(), settings));
}

// The same transponder with no flight ID, or no category, leaves what the pilot typed alone.
TEST_CASE("aircraft: a callsign or a type the transponder does not send is kept as it was") {
    Settings settings = go::defaults();
    std::strcpy(settings.callsign, "TYPED");
    settings.aircraft_type = 6;
    settings.own_icao = kHeardAddress;
    Detected detected = heard_glider();
    detected.callsign[0] = 0;
    detected.aircraft_type = 0;
    CHECK_FALSE(adopt(detected, settings));
    CHECK(settings.own_icao == kHeardAddress);
    CHECK(std::string(settings.callsign) == "TYPED");
    CHECK(settings.aircraft_type == 6);
}

// A Go moved to another aircraft must not put the last one's name on the air with this address.
TEST_CASE("aircraft: a new address heard with no callsign clears the one stored") {
    Settings settings = go::defaults();
    REQUIRE(adopt(heard_glider(), settings));
    Detected other = heard_glider();
    other.address = 0x3A0001;
    other.callsign[0] = 0;
    CHECK(adopt(other, settings));
    CHECK(settings.own_icao == 0x3A0001u);
    CHECK(settings.callsign[0] == 0);
}

TEST_CASE("aircraft: nothing but a transponder heard on the ground is ever adopted") {
    for (const Detection state : {Detection::Unpaired, Detection::NotConnected, Detection::Airborne,
                                  Detection::NoFix, Detection::Listening}) {
        Detected detected = heard_glider();
        detected.state = state;
        Settings settings = go::defaults();
        CHECK_FALSE(adopt(detected, settings));
        CHECK(settings.own_icao == kOwnIcaoUnset);
        CHECK_FALSE(matches(detected, settings));
    }
}

TEST_CASE("aircraft menu: what Bravo heard is on top, the three rows it sets under it") {
    MenuValues values = default_menu_values();
    values.detected = heard_glider();
    const Glass focused = aircraft_menu_of(values, MenuRow::Detected);
    CHECK(reads_heading(focused, "AIRCRAFT", Heading::Opened));
    CHECK(reads(focused, "ICAO 3A4B5C", kMenuScale, false));
    CHECK(reads_button_key(focused, "USE"));
    CHECK(reads(focused, "F-JABC", kCallsignScale, false));
    CHECK(reads(focused, "GLIDER", kMenuScale, false));
    for (int line = kDetectedLines; line < kMenuLines; line++)
        CHECK(reads_at(focused, kMenuLeftX, menu_line_text_y(line),
                       menu_row_label(aircraft_menu().rows[line - kDetectedLines + 1]),
                       kMenuScale));

    const Glass on_a_row = aircraft_menu_of(values, MenuRow::OwnIcao);
    CHECK(reads(on_a_row, "F-JABC", kCallsignScale));
    CHECK(reads_at(on_a_row, kMenuLeftX, menu_line_text_y(kMenuLines - 1), "OWN ICAO", kMenuScale,
                   false));
}

TEST_CASE(
    "aircraft menu: a transponder already stored reads SAME, and the press is spent on "
    "nothing") {
    MenuValues values = default_menu_values();
    values.detected = heard_glider();
    REQUIRE(adopt(values.detected, values.settings));
    const Glass canvas = aircraft_menu_of(values, MenuRow::OwnIcao);
    CHECK(reads(canvas, "SAME", kMenuScale));

    Bench bench;
    bench.values = values;
    bench.focus_on(MenuRow::Detected);
    CHECK(bench.change() == MenuAction::None);
    CHECK(bench.editor.aircraft());
}

TEST_CASE("aircraft menu: an address is shown where the transponder sends no callsign") {
    MenuValues values = default_menu_values();
    values.detected = heard_glider();
    values.detected.callsign[0] = 0;
    CHECK(reads(aircraft_menu_of(values, MenuRow::OwnIcao), "3A4B5C", kCallsignScale));
}

TEST_CASE("aircraft menu: each state with nothing to propose says why, and what to do") {
    const struct {
        Detection state;
        const char* reads;
        const char* detail;
    } cases[] = {
        {Detection::Unpaired, "NOT PAIRED", "PAIR A BRAVO FIRST."},
        {Detection::NotConnected, "NOT CONNECTED", nullptr},
        {Detection::Airborne, "IN FLIGHT", "BRAVO FINDS IT ON THE GROUND."},
        {Detection::NoFix, "NO FIX", "BRAVO FINDS IT ON THE GROUND."},
        {Detection::Listening, "NOT HEARD", "SET THE TRANSPONDER TO ALT."},
    };
    for (const auto& each : cases) {
        CAPTURE(each.reads);
        MenuValues values = default_menu_values();
        values.detected.state = each.state;
        const Glass canvas = aircraft_menu_of(values, MenuRow::OwnIcao);
        CHECK(reads(canvas, each.reads, kMenuScale));
        if (each.detail == nullptr) continue;
        char line[kFrameColumns + 1];
        for (int at = take_line(each.detail, 0, line, kFrameColumns); line[0] != 0;
             at = take_line(each.detail, at, line, kFrameColumns))
            CHECK(reads(canvas, line, kMenuScale));
    }
}

TEST_CASE("aircraft notice: it names what Bravo heard, and says what each contact does") {
    Glass canvas;
    draw_aircraft_notice(canvas, heard_glider());
    CHECK(reads_heading(canvas, kAircraftNoticeTitle, Heading::Interrupt));
    CHECK(reads(canvas, "F-JABC", kCallsignScale));
    CHECK(reads(canvas, "ICAO 3A4B5C", kMenuScale));
    CHECK(reads(canvas, "GLIDER", kMenuScale));
    CHECK(reads_button_key(canvas, "USE"));
    CHECK(reads_pad_key(canvas, "CLOSE"));
}

TEST_CASE("aircraft menu: the settings row opens it, and the pad past its last row comes back") {
    Bench bench;
    bench.walk_to(MenuRow::Aircraft);
    CHECK(bench.change() == MenuAction::Moved);
    REQUIRE(bench.editor.aircraft());
    CHECK(bench.editor.focus() == MenuRow::Detected);
    for (int i = 1; i < aircraft_menu().row_count; i++) CHECK(bench.move() == MenuAction::Moved);
    CHECK(bench.editor.focus() == MenuRow::OwnIcao);

    CHECK(bench.move() == MenuAction::Moved);
    CHECK_FALSE(bench.editor.aircraft());
    CHECK(bench.editor.focus() == MenuRow::Aircraft);
    CHECK(bench.editor.active());
}

// One press stores what the glass shows, and the settings row it came from reads the new name.
TEST_CASE("aircraft menu: a press on what Bravo heard stores it and goes back to settings") {
    Bench bench;
    bench.values.detected = heard_glider();
    bench.focus_on(MenuRow::Detected);
    CHECK(bench.change() == MenuAction::Changed);
    CHECK(bench.values.settings.own_icao == kHeardAddress);
    CHECK(std::string(bench.values.settings.callsign) == "F-JABC");
    CHECK_FALSE(bench.editor.aircraft());
    CHECK(bench.editor.focus() == MenuRow::Aircraft);
    char value[kMenuValueBytes];
    menu_row_value(value, MenuRow::Aircraft, bench.values);
    CHECK(std::string(value) == "F-JABC");
}

TEST_CASE("aircraft menu: a press with nothing heard changes nothing, unpaired it opens BRAVO") {
    Bench bench;
    bench.values.detected.state = Detection::Listening;
    bench.focus_on(MenuRow::Detected);
    CHECK(bench.change() == MenuAction::None);
    CHECK(bench.editor.aircraft());
    CHECK(bench.values.settings.own_icao == kOwnIcaoUnset);

    bench.values.detected.state = Detection::Unpaired;
    CHECK(bench.change() == MenuAction::Open);
    CHECK(bench.editor.opening() == Page::Bravo);
    CHECK_FALSE(bench.editor.active());
}
