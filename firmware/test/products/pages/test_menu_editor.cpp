// The menu editor: what the pad moves, what the button changes, and what a pilot types.
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

TEST_CASE("nearby menu: every row opens a page rather than changing a value") {
    const Menu menu = menu_for(Page::Nearby);
    REQUIRE(menu.n == 6);
    for (int i = 0; i < menu.n; i++) CHECK(opens_a_page(menu.rows[i]));
    CHECK(page_behind(MenuRow::RadioLog) == Page::RadioLog);
    CHECK(page_behind(MenuRow::Raw) == Page::Raw);
    CHECK(page_behind(MenuRow::Capture) == Page::Capture);
    CHECK(page_behind(MenuRow::Sats) == Page::Sats);
    CHECK(page_behind(MenuRow::Status) == Page::Status);
    CHECK(page_behind(MenuRow::SelfTest) == Page::SelfTest);

    // A press hands the page back to the caller and closes the menu behind it.
    Bench bench(Page::Nearby);
    bench.focus_on(MenuRow::Status);
    CHECK(bench.change() == MenuAction::Open);
    CHECK(bench.editor.opening() == Page::Status);
    CHECK_FALSE(bench.editor.active());
}

TEST_CASE("menu editor: the pad moves down a row, the button changes the row it is on") {
    Bench bench;
    CHECK(bench.editor.focus() == MenuRow::AircraftType);

    CHECK(bench.move() == MenuAction::Moved);
    CHECK(bench.editor.focus() == MenuRow::Callsign);
    CHECK(bench.move() == MenuAction::Moved);
    CHECK(bench.editor.focus() == MenuRow::Units);
    CHECK(bench.move() == MenuAction::Moved);
    CHECK(bench.editor.focus() == MenuRow::Range);
    CHECK(bench.move() == MenuAction::Moved);
    CHECK(bench.editor.focus() == MenuRow::Plot);
    CHECK(bench.move() == MenuAction::Moved);
    CHECK(bench.editor.focus() == MenuRow::Alarm);

    // The press acts on the row the focus is on, and leaves the focus there.
    CHECK(bench.values.settings.alarm_enabled);
    CHECK(bench.change() == MenuAction::Changed);
    CHECK(bench.editor.focus() == MenuRow::Alarm);
    CHECK_FALSE(bench.values.settings.alarm_enabled);

    // The next press toggles it back: one press, one step, no rhythm to get right.
    CHECK(bench.change() == MenuAction::Changed);
    CHECK(bench.values.settings.alarm_enabled);
}

TEST_CASE("menu editor: the callsign is rolled a character at a time and stored at the end") {
    Bench bench;
    bench.focus_on(MenuRow::Callsign);
    REQUIRE(bench.change() == MenuAction::Moved);
    REQUIRE(bench.editor.editing());
    CHECK(std::string(bench.editor.text()) == "         ");
    CHECK(bench.editor.cursor() == 0);

    // The pad rolls this character and moves nothing: the field is not the rows.
    const MenuRow focused = bench.editor.focus();
    for (int i = 0; i < 6; i++) bench.move();
    CHECK(bench.editor.focus() == focused);
    CHECK(bench.editor.text()[0] == 'E');
    CHECK(bench.editor.cursor() == 0);

    // The button steps to the next character, and the last one stores the name.
    CHECK(bench.change() == MenuAction::Moved);
    CHECK(bench.editor.cursor() == 1);
    bench.move();
    CHECK(bench.editor.text()[1] == '-');
    for (int i = 2; i < kCallsignChars; i++) CHECK(bench.change() == MenuAction::Moved);
    CHECK(bench.editor.cursor() == kCallsignChars - 1);

    CHECK(bench.change() == MenuAction::Changed);
    CHECK_FALSE(bench.editor.editing());
    CHECK(std::string(bench.values.settings.callsign) == "E-");
    // And the row it came from is the row the focus is still on.
    CHECK(bench.editor.focus() == MenuRow::Callsign);
}

// A name already stored is what the field opens on, not nine blanks.
TEST_CASE("menu editor: the callsign field opens on what is stored") {
    Bench bench;
    std::memcpy(bench.values.settings.callsign, "F-JABC", 7);
    bench.focus_on(MenuRow::Callsign);
    REQUIRE(bench.change() == MenuAction::Moved);
    CHECK(std::string(bench.editor.text()) == "F-JABC   ");

    // Walked through unchanged, it is stored as it was.
    for (int i = 1; i < kCallsignChars; i++) bench.change();
    CHECK(bench.change() == MenuAction::Changed);
    CHECK(std::string(bench.values.settings.callsign) == "F-JABC");
}

// The ring only rolls forward, so blanking nine characters is sixty taps nobody will spend.
TEST_CASE("menu editor: a press on a blank first character removes the stored name") {
    Bench bench;
    std::memcpy(bench.values.settings.callsign, "F-JABC", 7);
    bench.focus_on(MenuRow::Callsign);
    REQUIRE(bench.change() == MenuAction::Moved);

    for (int i = 0; i < 40 && bench.editor.text()[0] != ' '; i++) bench.move();
    REQUIRE(bench.editor.text()[0] == ' ');
    REQUIRE(bench.editor.cursor() == 0);

    CHECK(bench.change() == MenuAction::Changed);
    CHECK_FALSE(bench.editor.editing());
    CHECK(bench.values.settings.callsign[0] == 0);
    CHECK(bench.editor.focus() == MenuRow::Callsign);
}

// The same press on a device nobody has named: the field it opens blank is the field it closes.
TEST_CASE("menu editor: a blank first character on an unnamed device stores nothing") {
    Bench bench;
    bench.focus_on(MenuRow::Callsign);
    REQUIRE(bench.change() == MenuAction::Moved);
    REQUIRE(bench.editor.text()[0] == ' ');

    CHECK(bench.change() == MenuAction::Moved);
    CHECK_FALSE(bench.editor.editing());
    CHECK(bench.values.settings.callsign[0] == 0);
}

// The pad's long touch and the idle timer both leave, and neither stores.
TEST_CASE("menu editor: a field nobody finished stores nothing") {
    Bench bench;
    std::memcpy(bench.values.settings.callsign, "F-JABC", 7);
    bench.focus_on(MenuRow::Callsign);
    REQUIRE(bench.change() == MenuAction::Moved);
    for (int i = 0; i < 3; i++) bench.move();
    REQUIRE(bench.editor.text()[0] != 'F');

    CHECK(bench.run(MenuEditor::kIdleReturnMs + 100) == MenuAction::Leave);
    CHECK_FALSE(bench.editor.editing());
    CHECK(std::string(bench.values.settings.callsign) == "F-JABC");
}

TEST_CASE("menu editor: the focus only ever advances, and walks out of the menu") {
    Bench bench;
    for (int i = 1; i < bench.rows(); i++) {
        CHECK(bench.move() == MenuAction::Moved);
        CHECK(bench.editor.focus() == menu_for(Page::Radar).rows[i]);
    }
    REQUIRE(bench.editor.focus() == MenuRow::Volume);

    // One more tap leaves: a thumb that only knows the pad cannot be trapped here.
    CHECK(bench.move() == MenuAction::Leave);
    CHECK_FALSE(bench.editor.active());

    // The next entry starts at the top again, so the focus cycle is closed.
    bench.editor.enter(Page::Radar, bench.t);
    CHECK(bench.editor.focus() == MenuRow::AircraftType);
    CHECK(bench.editor.active());
}

TEST_CASE("menu editor: moving the focus over a row is not editing it") {
    // The abandoned edit: nothing is staged, so nothing is half applied.
    Bench bench;
    const go::Settings before = bench.values.settings;
    const int range_before = bench.values.settings.range_step;
    for (int i = 0; i < bench.rows(); i++) bench.move();
    CHECK_FALSE(bench.editor.active());
    CHECK(before.aircraft_type == bench.values.settings.aircraft_type);
    CHECK(before.alarm_enabled == bench.values.settings.alarm_enabled);
    CHECK(before.alarm_volume == bench.values.settings.alarm_volume);
    CHECK(before.units == bench.values.settings.units);
    CHECK(range_before == bench.values.settings.range_step);
}

TEST_CASE("menu editor: aircraft type walks the categories that name an aircraft") {
    Bench bench;
    bench.focus_on(MenuRow::AircraftType);
    REQUIRE(bench.values.settings.aircraft_type == go::kAircraftTypeLight);

    CHECK(bench.change() == MenuAction::Changed);
    CHECK(bench.values.settings.aircraft_type == 2);

    // Pressing on keeps stepping the same row rather than walking away from it.
    CHECK(bench.change() == MenuAction::Changed);
    CHECK(bench.values.settings.aircraft_type == 3);
    CHECK(bench.editor.focus() == MenuRow::AircraftType);

    // Every step is a code the page can name, and the list closes.
    for (int i = 0; i < kNamedAircraftTypes; i++) {
        CHECK(bench.values.settings.aircraft_type < kNamedAircraftTypes);
        CHECK(aircraft_type_name(bench.values.settings.aircraft_type)[0] != 0);
        CHECK(go::validate(bench.values.settings) == Status::Ok);
        bench.change();
    }
    CHECK(bench.values.settings.aircraft_type == 3);
}

TEST_CASE("menu editor: the volume a pilot can hear, and it stays inside what is valid") {
    Bench bench;
    bench.focus_on(MenuRow::Volume);
    REQUIRE(bench.values.settings.alarm_volume == 3);

    bench.change();
    CHECK(bench.values.settings.alarm_volume == 4);
    bench.change();
    CHECK(bench.values.settings.alarm_volume == 5);

    // Off the top it comes back to silent rather than to a value the validator would refuse.
    bench.change();
    CHECK(bench.values.settings.alarm_volume == 0);
    for (int i = 0; i <= kMaxAlarmVolume + 1; i++) {
        CHECK(bench.values.settings.alarm_volume <= kMaxAlarmVolume);
        CHECK(go::validate(bench.values.settings) == Status::Ok);
        bench.change();
    }
}

TEST_CASE("menu editor: the ring is a range a thumb can step, and the cycle closes") {
    Bench bench;
    bench.focus_on(MenuRow::Range);
    REQUIRE(bench.values.settings.range_step == kDefaultRangeStep);
    REQUIRE(range_value(bench.values.settings.range_step, Units::Nautical) == 4);

    CHECK(bench.change() == MenuAction::Changed);
    CHECK(range_value(bench.values.settings.range_step, Units::Nautical) == 8);
    CHECK(bench.change() == MenuAction::Changed);
    CHECK(range_value(bench.values.settings.range_step, Units::Nautical) == 1);

    // Every step is a range the radar can label, and the cycle comes back round.
    for (int i = 0; i < kRangeStepCount; i++) {
        CHECK(bench.values.settings.range_step >= 0);
        CHECK(bench.values.settings.range_step < kRangeStepCount);
        bench.change();
    }
    CHECK(range_value(bench.values.settings.range_step, Units::Nautical) == 1);

    // A step a companion app invented is not on the cycle, and the first press comes back to it.
    CHECK(next_range_step(37) == (kDefaultRangeStep + 1) % kRangeStepCount);
}

TEST_CASE("menu editor: the plot starts on all, and one press each way flips it to scale") {
    Bench bench;
    bench.focus_on(MenuRow::Plot);
    REQUIRE(bench.values.settings.plot == RadarPlot::All);

    CHECK(bench.change() == MenuAction::Changed);
    CHECK(bench.values.settings.plot == RadarPlot::ToScale);
    CHECK(go::validate(bench.values.settings) == Status::Ok);
    CHECK(bench.change() == MenuAction::Changed);
    CHECK(bench.values.settings.plot == RadarPlot::All);
}

TEST_CASE("menu editor: a value that would not validate is never handed back") {
    // The menu reads and does not write: it hands back no blob the firmware would refuse.
    Bench bench;
    bench.values.settings.aircraft_type = 200;
    bench.focus_on(MenuRow::Volume);
    REQUIRE(go::validate(bench.values.settings) != Status::Ok);

    const uint8_t volume = bench.values.settings.alarm_volume;
    CHECK(bench.change() == MenuAction::None);
    CHECK(bench.values.settings.alarm_volume == volume);
    CHECK(bench.values.settings.aircraft_type == 200);
}

TEST_CASE("menu editor: a menu nobody is pressing hands the traffic picture back") {
    Bench bench;
    bench.move();
    REQUIRE(bench.editor.active());

    CHECK(bench.run(MenuEditor::kIdleReturnMs - 1000) == MenuAction::None);
    CHECK(bench.editor.active());
    CHECK(bench.run(2000) == MenuAction::Leave);
    CHECK_FALSE(bench.editor.active());

    // Either contact resets the clock: a pilot working the rows is never dropped.
    bench.editor.enter(Page::Radar, bench.t);
    for (int i = 0; i < 3; i++) {
        bench.run(MenuEditor::kIdleReturnMs - 5000);
        bench.editor.button(bench.t);
        bench.run(100);
        bench.run(MenuEditor::kIdleReturnMs - 5000);
        bench.editor.pad(bench.t);
        bench.run(100);
    }
    CHECK(bench.editor.active());
}
