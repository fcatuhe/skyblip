// Harness, not a test: the menu editor on a clock the case advances, its values applied.
#ifndef SKYBLIP_TEST_SUPPORT_MENU_BENCH_H
#define SKYBLIP_TEST_SUPPORT_MENU_BENCH_H

#include <cstdint>

#include "doctest/doctest.h"
#include "products/skyblip_go/pages/menu.h"
#include "products/skyblip_go/settings.h"

namespace skyblip::go {

inline MenuValues fresh() {
    MenuValues v;
    v.settings = go::defaults();
    return v;
}

// The editor with a clock the case advances, and the values it hands back applied.
struct Bench {
    MenuEditor editor;
    MenuValues values{fresh()};
    uint32_t t{1000};
    Page page{Page::Radar};

    explicit Bench(Page on = Page::Radar) : page(on) { editor.enter(on, t); }

    MenuAction run(uint32_t ms) {
        MenuAction seen = MenuAction::None;
        for (uint32_t i = 0; i < ms; i += 10) {
            t += 10;
            MenuValues next;
            const MenuAction action = editor.tick(t, values, next);
            if (action == MenuAction::Changed) values = next;
            if (action != MenuAction::None) seen = action;
        }
        return seen;
    }

    // A tap of the pad: the focus moves down a row.
    MenuAction move() {
        editor.pad(t);
        return run(100);
    }

    // A press of the button: the focused row is acted on, and again on every further press.
    MenuAction change() {
        editor.button(t);
        return run(100);
    }

    int rows() const { return menu_for(page).n; }

    void focus_on(MenuRow row) {
        for (int i = 0; i < rows() && editor.focus() != row; i++) move();
        REQUIRE(editor.focus() == row);
    }
};

}  // namespace skyblip::go

#endif
