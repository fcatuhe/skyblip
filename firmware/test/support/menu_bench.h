// Harness, not a test: the menu editor on a clock the case advances, its values applied.
#ifndef SKYBLIP_TEST_SUPPORT_MENU_BENCH_H
#define SKYBLIP_TEST_SUPPORT_MENU_BENCH_H

#include <cstdint>

#include "doctest/doctest.h"
#include "products/skyblip_go/pages/menu.h"
#include "products/skyblip_go/settings.h"

namespace skyblip::go {

inline MenuValues default_menu_values() {
    MenuValues values;
    values.settings = go::defaults();
    return values;
}

// The editor with a clock the case advances, and the values it hands back applied.
struct Bench {
    MenuEditor editor;
    MenuValues values{default_menu_values()};
    uint32_t now_ms{1000};
    Page page{Page::Radar};

    explicit Bench(Page on = Page::Radar) : page(on) { editor.enter(on, now_ms); }

    MenuAction run(uint32_t for_ms) {
        MenuAction seen = MenuAction::None;
        for (uint32_t i = 0; i < for_ms; i += 10) {
            now_ms += 10;
            MenuValues next;
            const MenuAction action = editor.tick(now_ms, values, next);
            if (action == MenuAction::Changed || action == MenuAction::Light) values = next;
            if (action != MenuAction::None) seen = action;
        }
        return seen;
    }

    // A tap of the pad: the focus moves down a row.
    MenuAction move() {
        editor.pad(now_ms);
        return run(100);
    }

    // A press of the button: the focused row is acted on, and again on every further press.
    MenuAction change() {
        editor.button(now_ms);
        return run(100);
    }

    int rows() const { return editor.menu().row_count; }

    // A row of the AIRCRAFT menu is reached through the settings row that opens it.
    void focus_on(MenuRow row) {
        if (!editor.aircraft() && menu_row_index(aircraft_menu(), row) >= 0) {
            walk_to(MenuRow::Aircraft);
            REQUIRE(change() == MenuAction::Moved);
            REQUIRE(editor.aircraft());
        }
        walk_to(row);
    }

    void walk_to(MenuRow row) {
        for (int i = 0; i < rows() && editor.focus() != row; i++) move();
        REQUIRE(editor.focus() == row);
    }
};

}  // namespace skyblip::go

#endif
