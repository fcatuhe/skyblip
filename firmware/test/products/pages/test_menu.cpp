// The menu behind each page as drawn: the focused row, what each row reads, the callsign field.
#include <cstring>
#include <initializer_list>
#include <string>

#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/menu.h"
#include "products/skyblip_go/settings.h"
#include "test/support/menu_bench.h"

using namespace skyblip;
using namespace skyblip::go;

namespace {

constexpr int kGlyphH = 7;

int length(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

// Draw the same text at the same place in a scratch buffer and compare the box it occupies.
bool reads_at(const Glass& fb, int x, int y, const char* text, bool ink, int scale = 1) {
    Glass expected;
    expected.clear(true);
    if (!ink) expected.clear(false);
    expected.draw_text(x, y, text, ink, scale);
    for (int dy = 0; dy < kGlyphH * scale; dy++)
        for (int dx = 0; dx < length(text) * kSmallCellW * scale; dx++)
            if (fb.get_pixel(x + dx, y + dy) != expected.get_pixel(x + dx, y + dy)) return false;
    return true;
}

bool hint_reads(const Glass& fb, int y, const char* text, int scale) {
    return reads_at(fb, centred_x(text, scale), y, text, true, scale);
}

int line_of(Page page, MenuRow row) { return menu_row_index(menu_for(page), row); }

bool row_label_reads(const Glass& fb, Page page, MenuRow row, bool focused) {
    return reads_at(fb, kMenuLeftX, menu_line_text_y(line_of(page, row)), menu_row_label(row),
                    !focused, kMenuScale);
}

bool row_value_reads(const Glass& fb, Page page, MenuRow row, const char* value, bool focused) {
    return reads_at(fb, kMenuRightX - length(value) * kMenuCellW,
                    menu_line_text_y(line_of(page, row)), value, !focused, kMenuScale);
}

Glass page_of(Page page, const MenuValues& values, MenuRow focus) {
    MenuSnapshot snapshot;
    snapshot.page = page;
    snapshot.values = values;
    snapshot.focus = focus;
    Glass fb;
    draw_menu(fb, snapshot);
    return fb;
}

}  // namespace

TEST_CASE("radar menu: every row names what it holds, and the focused one is reversed out") {
    MenuValues values = fresh();
    values.settings.aircraft_type = 4;
    values.settings.alarm_volume = 3;

    const Glass fb = page_of(Page::Radar, values, MenuRow::Volume);

    CHECK(row_label_reads(fb, Page::Radar, MenuRow::AircraftType, false));
    CHECK(row_value_reads(fb, Page::Radar, MenuRow::AircraftType, "GLIDER", false));
    CHECK(row_label_reads(fb, Page::Radar, MenuRow::Alarm, false));
    CHECK(row_value_reads(fb, Page::Radar, MenuRow::Alarm, "ON", false));
    CHECK(row_value_reads(fb, Page::Radar, MenuRow::Range, "4 NM", false));
    CHECK(row_value_reads(fb, Page::Radar, MenuRow::Units, "NAUTICAL", false));

    // The focused row is white ink on a filled bar, told apart by shape before a word is read.
    CHECK(row_label_reads(fb, Page::Radar, MenuRow::Volume, true));
    CHECK(row_value_reads(fb, Page::Radar, MenuRow::Volume, "3 OF 5", true));
    CHECK_FALSE(row_label_reads(fb, Page::Radar, MenuRow::Volume, false));

    // Exactly one bar, and the page says how the two contacts work.
    const Menu menu = menu_for(Page::Radar);
    int bars = 0;
    for (int i = 0; i < menu.n; i++)
        if (row_label_reads(fb, Page::Radar, menu.rows[i], true)) bars++;
    CHECK(bars == 1);
    CHECK(reads_at(fb, kMenuHintX, kMenuHintY, kMenuHintText, true));

    // And nothing falls off a 200 pixel panel, on either axis.
    CHECK(menu_line_top(menu.n - 1) + kMenuRowHeight <= kMenuHintY);
    CHECK(kMenuHintY + kGlyphH < Glass::kH);

    // The hint is centred: the same air either side of it.
    const int hint_w = length(kMenuHintText) * kSmallCellW;
    CHECK(kMenuHintX > 0);
    CHECK(kMenuHintX - (Glass::kW - kMenuHintX - hint_w) <= 1);
}

TEST_CASE("menu: no label and value a row can hold meet at double height") {
    MenuValues values = fresh();
    for (Page page : {Page::Radar, Page::Nearby}) {
        const Menu menu = menu_for(page);
        for (int i = 0; i < menu.n; i++) {
            const int label_end = kMenuLeftX + length(menu_row_label(menu.rows[i])) * kMenuCellW;
            CHECK(label_end <= kMenuRightX);
            for (uint8_t type = 0; type < kNamedAircraftTypes + 2; type++) {
                values.settings.aircraft_type = type;
                for (Units units : {Units::Nautical, Units::Metric}) {
                    values.settings.units = units;
                    for (int step = 0; step < kRangeStepCount; step++) {
                        values.range_step = step;
                        char value[kMenuValueCap];
                        const int n = menu_row_value(value, menu.rows[i], values);
                        CHECK(label_end <= kMenuRightX - n * kMenuCellW);
                    }
                }
            }
        }
    }
}

TEST_CASE("menu: a menu is titled what it holds, not the page a thumb came from") {
    CHECK(std::strcmp(menu_title(Page::Radar), "SETTINGS") == 0);
    CHECK(std::strcmp(menu_title(Page::Nearby), "DIAGNOSTICS") == 0);

    const MenuValues values = fresh();
    for (Page page : {Page::Radar, Page::Nearby}) {
        const Glass fb = page_of(page, values, menu_for(page).rows[0]);
        CHECK(kMenuLeftX - 2 + length(menu_title(page)) * kMenuCellW <= Glass::kW);
        Glass expected;
        expected.clear(true);
        expected.draw_text(kMenuLeftX - 2, 3, menu_title(page), true, 2);
        for (int y = 3; y < 3 + kGlyphH * kMenuScale; y++)
            for (int x = 0; x < Glass::kW; x++)
                CHECK(fb.get_pixel(x, y) == expected.get_pixel(x, y));
    }
}

TEST_CASE("menu: a page with nothing behind it opens no menu at all") {
    for (Page page : {Page::SixPack, Page::GMeter, Page::Status, Page::Sats, Page::RadioLog,
                      Page::Raw, Page::SelfTest})
        CHECK(menu_for(page).n == 0);

    MenuEditor editor;
    editor.enter(Page::Status, 1000);
    CHECK_FALSE(editor.active());
}

TEST_CASE("menu: a category the phone stored but the page does not name is still shown") {
    MenuValues values = fresh();
    values.settings.aircraft_type = 13;
    const Glass fb = page_of(Page::Radar, values, MenuRow::Alarm);
    CHECK(row_value_reads(fb, Page::Radar, MenuRow::AircraftType, "CODE 13", false));

    // The first change moves it into the list the page can name.
    CHECK(next_aircraft_type(13) == 0);
}

TEST_CASE("menu: the UAV categories are not a choice a pilot can make on the panel") {
    MenuValues values = fresh();
    values.settings.aircraft_type = 11;
    const Glass fb = page_of(Page::Radar, values, MenuRow::Alarm);
    CHECK(row_value_reads(fb, Page::Radar, MenuRow::AircraftType, "CODE 11", false));
    CHECK(next_aircraft_type(11) == 0);
}

// The field a pilot types into, which is the one screen on this device that has a cursor.
TEST_CASE("callsign page: the characters stand at a size a thumb can check, with a bar under one") {
    CallsignSnapshot field;
    field.text = "F-JABC   ";
    field.cursor = 3;

    Glass fb;
    draw_callsign(fb, field);
    CHECK(reads_at(fb, kMenuLeftX - 2, 3, "CALLSIGN", true, kMenuScale));
    CHECK(reads_at(fb, kCallsignTextX, kCallsignTextY, "F-JABC", true, kCallsignScale));

    // The bar is under the character the pad is rolling and under no other.
    const int at = kCallsignTextX + field.cursor * kCallsignCellW;
    CHECK(fb.get_pixel(at, kCallsignCursorY));
    CHECK(fb.get_pixel(at + 5 * kCallsignScale - 1, kCallsignCursorY));
    CHECK_FALSE(fb.get_pixel(at - 2, kCallsignCursorY));
    CHECK_FALSE(fb.get_pixel(at + kCallsignCellW, kCallsignCursorY));

    // The two gestures at double height, and under them the one way out nothing else says.
    CHECK(hint_reads(fb, kCallsignPadHintY, kCallsignPadHintText, kCallsignHintScale));
    CHECK(hint_reads(fb, kCallsignButtonHintY, kCallsignNextHintText, kCallsignHintScale));
    CHECK(hint_reads(fb, kCallsignHelpY, kCallsignClearHelpText, 1));
    CHECK(kCallsignHelpY + kGlyphH < Glass::kH);
}

// A gesture nothing says is a gesture nobody finds, and this one throws the name away.
TEST_CASE("callsign page: the foot says the button clears when the bar is on a blank first one") {
    CallsignSnapshot field;
    field.text = "  JABC   ";
    field.cursor = 0;
    field.clears = callsign_press_clears(field.text, field.cursor);
    REQUIRE(field.clears);

    Glass fb;
    draw_callsign(fb, field);
    CHECK(hint_reads(fb, kCallsignButtonHintY, kCallsignClearHintText, kCallsignHintScale));
    // The line that taught the gesture goes once the gesture is armed.
    CHECK_FALSE(hint_reads(fb, kCallsignHelpY, kCallsignClearHelpText, 1));

    field.cursor = 1;
    field.clears = callsign_press_clears(field.text, field.cursor);
    CHECK_FALSE(field.clears);
    draw_callsign(fb, field);
    CHECK(hint_reads(fb, kCallsignButtonHintY, kCallsignNextHintText, kCallsignHintScale));
    CHECK(hint_reads(fb, kCallsignHelpY, kCallsignClearHelpText, 1));
}

// The ring a pad rolls: blank, dash, letters, digits, and round again.
TEST_CASE("menu: the callsign ring is blank, dash, A to Z, 0 to 9, and back") {
    CHECK(next_callsign_char(' ') == '-');
    CHECK(next_callsign_char('-') == 'A');
    CHECK(next_callsign_char('A') == 'B');
    CHECK(next_callsign_char('Z') == '0');
    CHECK(next_callsign_char('0') == '1');
    CHECK(next_callsign_char('9') == ' ');
}

// Nine characters are edited and what is stored is what a pilot typed.
TEST_CASE("menu: a stored callsign keeps its blanks inside and drops the ones at the end") {
    char out[kCallsignCap] = {0};
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

    const Glass fb = page_of(Page::Radar, bench.values, MenuRow::Range);
    CHECK(row_value_reads(fb, Page::Radar, MenuRow::Range, "8 KM", true));

    CHECK(bench.change() == MenuAction::Changed);
    CHECK(range_value(bench.values.range_step, Units::Metric) == 16);
    CHECK(range_metres(bench.values.range_step, Units::Metric) == 16000);

    // The same step read in the other unit is the ring it was drawn from.
    CHECK(range_value(bench.values.range_step, Units::Nautical) == 8);
    CHECK(range_metres(bench.values.range_step, Units::Nautical) == 8 * kMetresPerNm);
}
