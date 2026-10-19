// The menu behind each page as drawn: the focused row, what each row reads, the callsign field.
#include <cstring>
#include <initializer_list>
#include <string>

#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/menu.h"
#include "products/skyblip_go/settings.h"
#include "test/support/glass_text.h"
#include "test/support/menu_bench.h"

using namespace skyblip;
using namespace skyblip::go;

namespace {

using ui::kCellW;
using ui::kCharH;

bool hint_reads(const Glass& canvas, int y, const char* text, int scale) {
    return reads_at(canvas, centred_x(text, scale), y, text, scale);
}

// A menu as drawn, with the focus that decided which of its rows are on the glass.
struct Drawn {
    Glass canvas;
    Page page{Page::Radar};
    MenuRow focus{MenuRow::Light};
};

int line_of(const Drawn& drawn, MenuRow row) {
    const Menu menu = menu_for(drawn.page);
    return menu_row_index(menu, row) - menu_first_row(menu, drawn.focus);
}

bool row_label_reads(const Drawn& drawn, MenuRow row, bool focused) {
    return reads_at(drawn.canvas, kMenuLeftX, menu_line_text_y(line_of(drawn, row)),
                    menu_row_label(row), kMenuScale, !focused);
}

bool row_value_reads(const Drawn& drawn, MenuRow row, const char* value, bool focused) {
    return reads_at(drawn.canvas, kMenuRightX - static_cast<int>(std::strlen(value)) * kMenuCellW,
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

}  // namespace

TEST_CASE("radar menu: every row names what it holds, and the focused one is reversed out") {
    MenuValues values = default_menu_values();
    values.settings.aircraft_type = 4;
    values.settings.alarm_volume = go::AlarmVolume::Soft;

    const Drawn drawn = page_of(Page::Radar, values, MenuRow::Alarm);

    CHECK(row_label_reads(drawn, MenuRow::Light, false));
    CHECK(row_value_reads(drawn, MenuRow::Light, "OFF", false));
    CHECK(row_label_reads(drawn, MenuRow::AircraftType, false));
    CHECK(row_value_reads(drawn, MenuRow::AircraftType, "GLIDER", false));
    CHECK(row_value_reads(drawn, MenuRow::Range, "4 NM", false));
    CHECK(row_value_reads(drawn, MenuRow::Plot, "ALL", false));
    CHECK(row_value_reads(drawn, MenuRow::Units, "NAUTICAL", false));

    // The focused row is white ink on a filled bar, told apart by shape before a word is read.
    CHECK(row_label_reads(drawn, MenuRow::Alarm, true));
    CHECK(row_value_reads(drawn, MenuRow::Alarm, "ON", true));
    CHECK_FALSE(row_label_reads(drawn, MenuRow::Alarm, false));

    // Exactly one bar, and the page says how the two contacts work.
    int bars = 0;
    for (int line = 0; line < kMenuLines; line++)
        if (row_label_reads(drawn, menu_for(Page::Radar).rows[line], true)) bars++;
    CHECK(bars == 1);
    CHECK(reads_at(drawn.canvas, kMenuHintX, kMenuHintY, kMenuHintText));

    // And nothing falls off a 200 pixel panel, on either axis.
    CHECK(menu_line_top(kMenuLines - 1) + kMenuRowHeight <= kMenuHintY);
    CHECK(kMenuHintY + kCharH < Glass::kH);

    // The hint is centred: the same air either side of it.
    const int hint_w = static_cast<int>(std::strlen(kMenuHintText)) * kCellW;
    CHECK(kMenuHintX > 0);
    CHECK(kMenuHintX - (Glass::kW - kMenuHintX - hint_w) <= 1);

    // And a row's words stand in the middle of its bar, the same air above and below.
    CHECK(2 * kMenuTextInset + kCharH * kMenuScale == kMenuBarH);
}

// Eight rows and seven lines: the rows slide up under the focus, one line at a time.
TEST_CASE("radar menu: the focus past the last line slides the rows up and keeps the hint") {
    MenuValues values = default_menu_values();
    values.settings.alarm_volume = go::AlarmVolume::Soft;
    const Menu menu = menu_for(Page::Radar);
    REQUIRE(menu.row_count == kMenuLines + 1);

    const Drawn top = page_of(Page::Radar, values, MenuRow::Light);
    CHECK(line_of(top, MenuRow::Light) == 0);
    CHECK(line_of(top, MenuRow::Volume) == kMenuLines);
    CHECK(row_value_reads(top, MenuRow::Alarm, "ON", false));
    CHECK(reads_at(top.canvas, kMenuHintX, kMenuHintY, kMenuHintText));

    const Drawn slid = page_of(Page::Radar, values, MenuRow::Volume);
    CHECK(line_of(slid, MenuRow::Volume) == kMenuLines - 1);
    CHECK(row_label_reads(slid, MenuRow::Volume, true));
    CHECK(row_value_reads(slid, MenuRow::Volume, "SOFT", true));
    CHECK(row_label_reads(slid, MenuRow::AircraftType, false));
    CHECK(line_of(slid, MenuRow::AircraftType) == 0);
    CHECK(reads_at(slid.canvas, kMenuHintX, kMenuHintY, kMenuHintText));
}

TEST_CASE("menu page: no label and value a row can hold meet at double height") {
    MenuValues values = default_menu_values();
    for (Page page : {Page::Radar, Page::Nearby}) {
        const Menu menu = menu_for(page);
        for (int i = 0; i < menu.row_count; i++) {
            const int label_end =
                kMenuLeftX +
                static_cast<int>(std::strlen(menu_row_label(menu.rows[i]))) * kMenuCellW;
            CHECK(label_end <= kMenuRightX);
            for (uint8_t type = 0; type < kNamedAircraftTypes + 2; type++) {
                values.settings.aircraft_type = type;
                for (Units units : {Units::Nautical, Units::Metric}) {
                    values.settings.units = units;
                    for (int step = 0; step < kRangeStepCount; step++) {
                        values.settings.range_step = step;
                        char value[kMenuValueBytes];
                        const int n = menu_row_value(value, menu.rows[i], values);
                        CHECK(label_end <= kMenuRightX - n * kMenuCellW);
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
        CHECK(kMenuLeftX - 2 + static_cast<int>(std::strlen(menu_title(page))) * kMenuCellW <=
              Glass::kW);
        Glass expected;
        expected.clear(true);
        expected.draw_text(kMenuLeftX - 2, 3, menu_title(page), true, 2);
        for (int y = 3; y < 3 + kCharH * kMenuScale; y++)
            for (int x = 0; x < Glass::kW; x++)
                CHECK(drawn.canvas.get_pixel(x, y) == expected.get_pixel(x, y));
    }
}

TEST_CASE("menu page: a page with nothing behind it opens no menu at all") {
    for (Page page : {Page::SixPack, Page::GMeter, Page::Status, Page::Sats, Page::RadioLog,
                      Page::Raw, Page::SelfTest})
        CHECK(menu_for(page).row_count == 0);

    MenuEditor editor;
    editor.enter(Page::Status, 1000);
    CHECK_FALSE(editor.active());
}

TEST_CASE("menu page: a category the phone stored but the page does not name is still shown") {
    MenuValues values = default_menu_values();
    values.settings.aircraft_type = 13;
    const Drawn menu = page_of(Page::Radar, values, MenuRow::Alarm);
    CHECK(row_value_reads(menu, MenuRow::AircraftType, "CODE 13", false));

    // The first change moves it into the list the page can name.
    CHECK(next_aircraft_type(13) == 0);
}

TEST_CASE("menu page: the UAV categories are not a choice a pilot can make on the panel") {
    MenuValues values = default_menu_values();
    values.settings.aircraft_type = 11;
    const Drawn menu = page_of(Page::Radar, values, MenuRow::Alarm);
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
    CHECK(reads_at(canvas, kMenuLeftX - 2, 3, "CALLSIGN", kMenuScale));
    CHECK(reads_at(canvas, kCallsignTextX, kCallsignTextY, "F-JABC", kCallsignScale));

    // The bar is under the character the pad is rolling and under no other.
    const int at = kCallsignTextX + field.cursor * kCallsignCellW;
    CHECK(canvas.get_pixel(at, kCallsignCursorY));
    CHECK(canvas.get_pixel(at + 5 * kCallsignScale - 1, kCallsignCursorY));
    CHECK_FALSE(canvas.get_pixel(at - 2, kCallsignCursorY));
    CHECK_FALSE(canvas.get_pixel(at + kCallsignCellW, kCallsignCursorY));

    // The two gestures at double height, and under them the one way out nothing else says.
    CHECK(hint_reads(canvas, kCallsignPadHintY, kCallsignPadHintText, kCallsignHintScale));
    CHECK(hint_reads(canvas, kCallsignButtonHintY, kCallsignNextHintText, kCallsignHintScale));
    CHECK(hint_reads(canvas, kCallsignHelpY, kCallsignClearHelpText, 1));
    CHECK(kCallsignHelpY + kCharH < Glass::kH);
}

// A gesture nothing says is a gesture nobody finds, and this one throws the name away.
TEST_CASE("callsign page: the foot says the button clears when the bar is on a blank first one") {
    CallsignSnapshot field;
    field.text = "  JABC   ";
    field.cursor = 0;
    field.clears = callsign_press_clears(field.text, field.cursor);
    REQUIRE(field.clears);

    Glass canvas;
    draw_callsign(canvas, field);
    CHECK(hint_reads(canvas, kCallsignButtonHintY, kCallsignClearHintText, kCallsignHintScale));
    // The line that taught the gesture goes once the gesture is armed.
    CHECK_FALSE(hint_reads(canvas, kCallsignHelpY, kCallsignClearHelpText, 1));

    field.cursor = 1;
    field.clears = callsign_press_clears(field.text, field.cursor);
    CHECK_FALSE(field.clears);
    draw_callsign(canvas, field);
    CHECK(hint_reads(canvas, kCallsignButtonHintY, kCallsignNextHintText, kCallsignHintScale));
    CHECK(hint_reads(canvas, kCallsignHelpY, kCallsignClearHelpText, 1));
}

// The ring a pad rolls: blank, dash, letters, digits, and round again.
TEST_CASE("menu page: the callsign ring is blank, dash, A to Z, 0 to 9, and back") {
    CHECK(next_callsign_char(' ') == '-');
    CHECK(next_callsign_char('-') == 'A');
    CHECK(next_callsign_char('A') == 'B');
    CHECK(next_callsign_char('Z') == '0');
    CHECK(next_callsign_char('0') == '1');
    CHECK(next_callsign_char('9') == ' ');
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
