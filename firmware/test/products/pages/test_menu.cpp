// The menu behind each page as drawn: the focused row, what each row reads, the callsign field.
#include <cstring>
#include <initializer_list>
#include <string>

#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/menu.h"
#include "products/skyblip_go/settings.h"
#include "test/support/frame_glass.h"
#include "test/support/glass_text.h"
#include "test/support/menu_bench.h"

using namespace skyblip;
using namespace skyblip::go;

namespace {

using ui::kCellWidth;
using ui::kCharacterHeight;

constexpr int kOwnIcaoTextX = (Glass::kWidth - kOwnIcaoCharacters * kCallsignCellWidth) / 2;

// A menu as drawn, with the focus that decided which of its rows are on the glass.
struct Drawn {
    Glass canvas;
    Page page{Page::Radar};
    MenuRow focus{MenuRow::Light};
    bool aircraft{false};
};

int line_of(const Drawn& drawn, MenuRow row) {
    if (drawn.aircraft) return kDetectedLines + menu_row_index(aircraft_menu(), row) - 1;
    const Menu menu = menu_for(drawn.page);
    return menu_row_index(menu, row) - menu_first_row(menu, drawn.focus);
}

bool row_label_reads(const Drawn& drawn, MenuRow row, bool focused) {
    return reads_at(drawn.canvas, kMenuLeftX, menu_line_text_y(line_of(drawn, row)),
                    menu_row_label(row), kMenuScale, !focused);
}

bool row_value_reads(const Drawn& drawn, MenuRow row, const char* value, bool focused) {
    return reads_at(drawn.canvas,
                    kMenuRightX - static_cast<int>(std::strlen(value)) * kMenuCellWidth,
                    menu_line_text_y(line_of(drawn, row)), value, kMenuScale, !focused);
}

Drawn page_of(Page page, const MenuValues& values, MenuRow focus) {
    MenuSnapshot snapshot;
    snapshot.page = page;
    snapshot.values = values;
    snapshot.focus = focus;
    Drawn drawn;
    drawn.page = page;
    drawn.focus = focus;
    draw_menu(drawn.canvas, snapshot);
    return drawn;
}

Drawn aircraft_of(const MenuValues& values, MenuRow focus) {
    MenuSnapshot snapshot;
    snapshot.values = values;
    snapshot.focus = focus;
    snapshot.aircraft = true;
    Drawn drawn;
    drawn.focus = focus;
    drawn.aircraft = true;
    draw_menu(drawn.canvas, snapshot);
    return drawn;
}

}  // namespace

TEST_CASE("radar menu: every row names what it holds, and the focused one is reversed out") {
    MenuValues values = default_menu_values();
    std::strcpy(values.settings.callsign, "F-JABCDEF");
    values.settings.alarm_volume = go::AlarmVolume::Soft;

    const Drawn drawn = page_of(Page::Radar, values, MenuRow::Alarm);

    CHECK(row_label_reads(drawn, MenuRow::Light, false));
    CHECK(row_value_reads(drawn, MenuRow::Light, "OFF", false));
    CHECK(row_label_reads(drawn, MenuRow::Aircraft, false));
    CHECK(row_value_reads(drawn, MenuRow::Aircraft, "F-JABC", false));
    CHECK(row_value_reads(drawn, MenuRow::Range, "4 NM", false));
    CHECK(row_value_reads(drawn, MenuRow::Plot, "ALL", false));
    CHECK(row_value_reads(drawn, MenuRow::Units, "NAUTICAL", false));

    // The focused row is white ink on a filled bar, told apart by shape before a word is read.
    CHECK(row_label_reads(drawn, MenuRow::Alarm, true));
    CHECK(row_value_reads(drawn, MenuRow::Alarm, "ON", true));
    CHECK_FALSE(row_label_reads(drawn, MenuRow::Alarm, false));

    // Exactly one bar, and the keys say what each contact does to it.
    int bars = 0;
    for (int line = 0; line < kMenuLines; line++)
        if (row_label_reads(drawn, menu_for(Page::Radar).rows[line], true)) bars++;
    CHECK(bars == 1);
    CHECK(reads_heading(drawn.canvas, "SETTINGS", Heading::Opened));
    CHECK(reads_button_key(drawn.canvas, "CHANGE"));
    CHECK(reads_pad_key(drawn.canvas, "NEXT"));

    // And the rows stand clear of the keys.
    CHECK(menu_line_top(kMenuLines - 1) + kMenuRowHeight <= kKeyTop);

    // And a row's words stand in the middle of its bar, the same air above and below.
    CHECK(2 * kMenuTextInset + kCharacterHeight * kMenuScale == kMenuBarHeight);
}

// Eight rows and seven lines: the rows slide up under the focus, one line at a time.
TEST_CASE("radar menu: the focus past the last line slides the rows up and keeps the hint") {
    MenuValues values = default_menu_values();
    values.settings.alarm_volume = go::AlarmVolume::Soft;
    const Menu menu = menu_for(Page::Radar);
    REQUIRE(menu.row_count == kMenuLines + 1);

    const Drawn top = page_of(Page::Radar, values, MenuRow::Light);
    CHECK(line_of(top, MenuRow::Light) == 0);
    CHECK(line_of(top, MenuRow::Volume) == kMenuLines - 1);
    CHECK(line_of(top, MenuRow::Bravo) == kMenuLines);
    CHECK(row_value_reads(top, MenuRow::Alarm, "ON", false));
    CHECK(reads_pad_key(top.canvas, "NEXT"));

    const Drawn last = page_of(Page::Radar, values, MenuRow::Bravo);
    CHECK(line_of(last, MenuRow::Bravo) == kMenuLines - 1);
    CHECK(row_label_reads(last, MenuRow::Bravo, true));
    CHECK(row_value_reads(last, MenuRow::Bravo, "NONE", true));
    CHECK(row_value_reads(last, MenuRow::Volume, "SOFT", false));
    CHECK(row_label_reads(last, MenuRow::Aircraft, false));
    CHECK(line_of(last, MenuRow::Aircraft) == 0);
    // Off the last row the pad hands the glass back, and the key says so.
    CHECK(reads_pad_key(last.canvas, "BACK"));
}

TEST_CASE("menu page: the button's key names what a press does to the focused row") {
    const MenuValues values = default_menu_values();
    CHECK(reads_button_key(page_of(Page::Radar, values, MenuRow::Units).canvas, "CHANGE"));
    CHECK(reads_button_key(page_of(Page::Radar, values, MenuRow::Bravo).canvas, "OPEN"));
    CHECK(reads_button_key(page_of(Page::Radar, values, MenuRow::Aircraft).canvas, "OPEN"));
    CHECK(reads_button_key(page_of(Page::Nearby, values, MenuRow::Status).canvas, "OPEN"));
    CHECK(reads_heading(page_of(Page::Nearby, values, MenuRow::Status).canvas, "DIAGNOSTICS",
                        Heading::Opened));
}

// The address a controller reads off the aircraft's papers, six hex digits as they are written.
TEST_CASE("radar menu: the own ICAO row shows the six hex digits stored, or NONE") {
    MenuValues values = default_menu_values();
    char value[kMenuValueBytes];
    menu_row_value(value, MenuRow::OwnIcao, values);
    CHECK(std::string(value) == "NONE");
    values.settings.own_icao = 0x3C6586;
    menu_row_value(value, MenuRow::OwnIcao, values);
    CHECK(std::string(value) == "3C6586");
    values.settings.own_icao = 0x00A0F1;
    menu_row_value(value, MenuRow::OwnIcao, values);
    CHECK(std::string(value) == "00A0F1");

    const Drawn drawn = aircraft_of(values, MenuRow::OwnIcao);
    CHECK(row_label_reads(drawn, MenuRow::OwnIcao, true));
    CHECK(row_value_reads(drawn, MenuRow::OwnIcao, "00A0F1", true));
}

TEST_CASE("menu page: no label and value a row can hold meet at double height") {
    MenuValues values = default_menu_values();
    std::strcpy(values.settings.callsign, "F-JABCDEF");
    values.settings.own_icao = 0xFFFFFF;
    for (const Menu& menu : {menu_for(Page::Radar), menu_for(Page::Nearby), aircraft_menu()}) {
        for (int i = 0; i < menu.row_count; i++) {
            if (menu.rows[i] == MenuRow::Detected) continue;
            const int label_end =
                kMenuLeftX +
                static_cast<int>(std::strlen(menu_row_label(menu.rows[i]))) * kMenuCellWidth;
            CHECK(label_end <= kMenuRightX);
            for (uint8_t type = 0; type < 18; type++) {
                values.settings.aircraft_type = type;
                for (Units units : {Units::Nautical, Units::Metric}) {
                    values.settings.units = units;
                    for (int step = 0; step < kRangeStepCount; step++) {
                        values.settings.range_step = step;
                        char value[kMenuValueBytes];
                        const int written = menu_row_value(value, menu.rows[i], values);
                        CHECK(label_end <= kMenuRightX - written * kMenuCellWidth);
                    }
                }
            }
        }
    }
}

TEST_CASE("menu page: a menu is titled what it holds, not the page a thumb came from") {
    CHECK(std::strcmp(menu_title(Page::Radar), "SETTINGS") == 0);
    CHECK(std::strcmp(menu_title(Page::Nearby), "DIAGNOSTICS") == 0);

    const MenuValues values = default_menu_values();
    for (Page page : {Page::Radar, Page::Nearby}) {
        const Drawn drawn = page_of(page, values, menu_for(page).rows[0]);
        CHECK(fits_a_line(menu_title(page)));
        CHECK(reads_heading(drawn.canvas, menu_title(page), Heading::Opened));
    }
}

TEST_CASE("menu page: a page with nothing behind it opens no menu at all") {
    for (Page page : {Page::SixPack, Page::GMeter, Page::Status, Page::Satellites, Page::RadioLog,
                      Page::BravoLog, Page::SelfTest})
        CHECK(menu_for(page).row_count == 0);

    MenuEditor editor;
    editor.enter(Page::Status, 1000);
    CHECK_FALSE(editor.active());
}

TEST_CASE("menu page: a category the phone stored but the page does not name is still shown") {
    MenuValues values = default_menu_values();
    values.settings.aircraft_type = 13;
    const Drawn menu = aircraft_of(values, MenuRow::OwnIcao);
    CHECK(row_value_reads(menu, MenuRow::AircraftType, "CODE 13", false));

    // The first change moves it into the list the page can name.
    CHECK(next_aircraft_type(13) == 0);
}

// Issue 2 splits the helicopter by weight and adds the hang-glider and the paramotor, all flown
// seated.
TEST_CASE(
    "menu page: Issue 2's rotorcraft, hang-glider and paramotor are choices, and the list closes") {
    const struct {
        uint8_t code;
        const char* name;
    } kIssue2[] = {{3, "LIGHT HELI"}, {15, "HEAVY HELI"}, {16, "HANG GLIDER"}, {17, "PARAMOTOR"}};
    for (const auto& type : kIssue2) {
        CAPTURE(int(type.code));
        MenuValues values = default_menu_values();
        values.settings.aircraft_type = type.code;
        const Drawn menu = aircraft_of(values, MenuRow::OwnIcao);
        CHECK(row_value_reads(menu, MenuRow::AircraftType, type.name, false));
    }
    CHECK(next_aircraft_type(10) == 15);
    CHECK(next_aircraft_type(15) == 16);
    CHECK(next_aircraft_type(16) == 17);
    CHECK(next_aircraft_type(17) == 0);
}

TEST_CASE("menu page: the UAV categories are not a choice a pilot can make on the panel") {
    MenuValues values = default_menu_values();
    values.settings.aircraft_type = 11;
    const Drawn menu = aircraft_of(values, MenuRow::OwnIcao);
    CHECK(row_value_reads(menu, MenuRow::AircraftType, "CODE 11", false));
    CHECK(next_aircraft_type(11) == 0);
}

// The field a pilot types into, which is the one screen on this device that has a cursor.
TEST_CASE("callsign page: the characters stand at a size a thumb can check, with a bar under one") {
    CallsignSnapshot field;
    field.text = "F-JABC   ";
    field.cursor = 3;

    Glass canvas;
    draw_callsign(canvas, field);
    CHECK(reads_heading(canvas, "CALLSIGN", Heading::Opened));
    CHECK(reads_at(canvas, kCallsignTextX, kCallsignTextY, "F-JABC", kCallsignScale));

    // The bar is under the character the pad is rolling and under no other.
    const int at = kCallsignTextX + field.cursor * kCallsignCellWidth;
    CHECK(canvas.get_pixel(at, kCallsignCursorY));
    CHECK(canvas.get_pixel(at + 5 * kCallsignScale - 1, kCallsignCursorY));
    CHECK_FALSE(canvas.get_pixel(at - 2, kCallsignCursorY));
    CHECK_FALSE(canvas.get_pixel(at + kCallsignCellWidth, kCallsignCursorY));

    // The gestures swap over here, and the keys say so: the pad changes, the button moves on.
    CHECK(reads_pad_key(canvas, kFieldChangeVerb));
    CHECK(reads_button_key(canvas, kFieldNextVerb));
}

TEST_CASE("callsign page: on the last character the button's key says it saves") {
    CallsignSnapshot field;
    field.text = "F-JABC   ";
    field.cursor = 8;
    Glass canvas;
    draw_callsign(canvas, field);
    CHECK(reads_button_key(canvas, kFieldSaveVerb));
    CHECK_FALSE(reads_button_key(canvas, kFieldNextVerb));
}

// A gesture nothing says is a gesture nobody finds, and this one throws the name away.
TEST_CASE("callsign page: the button's key says it erases when the bar is on a blank first one") {
    CallsignSnapshot field;
    field.text = "  JABC   ";
    field.cursor = 0;
    field.clears = callsign_press_clears(field.text, field.cursor);
    REQUIRE(field.clears);

    Glass canvas;
    draw_callsign(canvas, field);
    CHECK(reads_button_key(canvas, kFieldEraseVerb));

    field.cursor = 1;
    field.clears = callsign_press_clears(field.text, field.cursor);
    CHECK_FALSE(field.clears);
    draw_callsign(canvas, field);
    CHECK(reads_button_key(canvas, kFieldNextVerb));
    CHECK_FALSE(reads_button_key(canvas, kFieldEraseVerb));
}

// The callsign's field, typing six hex digits: a transponder address is written that way.
TEST_CASE("own ICAO page: six hex digits stand centred at the callsign's size, under its title") {
    CallsignSnapshot field;
    field.title = menu_row_label(MenuRow::OwnIcao);
    field.text = "3C6586";
    field.cursor = 5;

    Glass canvas;
    draw_callsign(canvas, field);
    CHECK(reads_heading(canvas, "OWN ICAO", Heading::Opened));
    CHECK(reads_at(canvas, kOwnIcaoTextX, kCallsignTextY, "3C6586", kCallsignScale));
    CHECK(kOwnIcaoTextX - (Glass::kWidth - kOwnIcaoTextX - 6 * kCallsignCellWidth) <= 1);

    const int at = kOwnIcaoTextX + field.cursor * kCallsignCellWidth;
    CHECK(canvas.get_pixel(at, kCallsignCursorY));
    CHECK_FALSE(canvas.get_pixel(at - 2, kCallsignCursorY));
    CHECK(reads_pad_key(canvas, kFieldChangeVerb));
    CHECK(reads_button_key(canvas, kFieldSaveVerb));
}

// Without the own ICAO the aircraft's own transponder can alarm, so a clear is asked twice.
TEST_CASE("own ICAO page: a clear shows the address it throws away and what that costs") {
    ClearSnapshot clear;
    clear.title = menu_row_label(MenuRow::OwnIcao);
    clear.text = "3C6586";

    Glass canvas;
    draw_clear(canvas, clear);
    CHECK(reads_heading(canvas, "OWN ICAO", Heading::Opened));
    CHECK(reads_pad_key(canvas, kFieldKeepVerb));
    CHECK(reads_button_key(canvas, kFieldEraseVerb));
    const BodyLine body[] = {{"3C6586", kCallsignScale},
                             {""},
                             {kOwnIcaoClearLines[0]},
                             {kOwnIcaoClearLines[1]},
                             {kOwnIcaoClearLines[2]}};
    CHECK(reads_body(canvas, Heading::Opened, true, body, 5));
}

TEST_CASE("menu page: the own ICAO ring is blank, 0 to 9, A to F, and back") {
    CHECK(next_hex_character(' ') == '0');
    CHECK(next_hex_character('0') == '1');
    CHECK(next_hex_character('9') == 'A');
    CHECK(next_hex_character('A') == 'B');
    CHECK(next_hex_character('F') == ' ');
}

// 000000 is no address, and 0 is what the setting holds while it is unset.
TEST_CASE("menu page: six typed hex digits are an address, and a blank or 000000 is none") {
    CHECK(own_icao_typed("3C6586") == 0x3C6586u);
    CHECK(own_icao_typed("FFFFFF") == 0xFFFFFFu);
    CHECK(own_icao_typed("00A0F1") == 0x00A0F1u);
    CHECK(own_icao_typed("3C65 6") == kOwnIcaoUnset);
    CHECK(own_icao_typed("      ") == kOwnIcaoUnset);
    CHECK(own_icao_typed("000000") == kOwnIcaoUnset);
}

// The ring a pad rolls: blank, dash, letters, digits, and round again.
TEST_CASE("menu page: the callsign ring is blank, dash, A to Z, 0 to 9, and back") {
    CHECK(next_callsign_character(' ') == '-');
    CHECK(next_callsign_character('-') == 'A');
    CHECK(next_callsign_character('A') == 'B');
    CHECK(next_callsign_character('Z') == '0');
    CHECK(next_callsign_character('0') == '1');
    CHECK(next_callsign_character('9') == ' ');
}

// Nine characters are edited and what is stored is what a pilot typed.
TEST_CASE("menu page: a stored callsign keeps its blanks inside and drops the ones at the end") {
    char out[kCallsignBytes] = {0};
    CHECK(callsign_stored(out, "F-JABC   ") == 6);
    CHECK(std::string(out) == "F-JABC");
    CHECK(callsign_stored(out, "         ") == 0);
    CHECK(out[0] == 0);
    CHECK(callsign_stored(out, "F-J ABC  ") == 7);
    CHECK(std::string(out) == "F-J ABC");
    CHECK(callsign_stored(out, "ABCDEFGHI") == 9);
    CHECK(std::string(out) == "ABCDEFGHI");
}

// The ring is picked in the unit it is read in, so switching units keeps the step a pilot chose.
TEST_CASE("menu editor: a metric pilot steps whole kilometres, not a converted mile") {
    Bench bench;
    bench.values.settings.units = Units::Metric;
    bench.focus_on(MenuRow::Range);

    const Drawn menu = page_of(Page::Radar, bench.values, MenuRow::Range);
    CHECK(row_value_reads(menu, MenuRow::Range, "8 KM", true));

    CHECK(bench.change() == MenuAction::Changed);
    CHECK(range_value(bench.values.settings.range_step, Units::Metric) == 16);
    CHECK(range_metres(bench.values.settings.range_step, Units::Metric) == 16000);

    // The same step read in the other unit is the ring it was drawn from.
    CHECK(range_value(bench.values.settings.range_step, Units::Nautical) == 8);
    CHECK(range_metres(bench.values.settings.range_step, Units::Nautical) == 8 * kMetresPerNm);
}
