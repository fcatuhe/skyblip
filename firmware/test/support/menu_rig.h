// Harness, not a test: the settings menu driven through the whole product, and the blob it stored.
#ifndef SKYBLIP_TEST_SUPPORT_MENU_RIG_H
#define SKYBLIP_TEST_SUPPORT_MENU_RIG_H

#include <cstddef>
#include <cstdint>

#include "core/timing/durable_write.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/input/gesture.h"
#include "products/skyblip_go/pages/menu.h"
#include "products/skyblip_go/settings.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

constexpr uint32_t kDoublePressWindowMs = go::ConfirmGesture::kDoublePressWindowMs;

// Long enough for a press to have finished meaning one thing before the next
// one is made: the pairing window, and a little.
inline void settle(Rig& rig, uint32_t& now_ms) {
    rig.run(now_ms, now_ms + kDoublePressWindowMs + 200);
    now_ms += kDoublePressWindowMs + 200;
}

inline void push_fix(Rig& rig, int32_t speed_mm_s, int32_t altitude_msl_m) {
    gnss::GnssSolution solution{};
    solution.fix_valid = true;
    solution.speed_mm_s = speed_mm_s;
    solution.altitude_msl_mm = altitude_msl_m * 1000;
    solution.sentences = 1;
    rig.product.bus().gnss.push(solution);
}

inline void open_menu(Rig& rig, uint32_t& now_ms) {
    rig.press(now_ms);
    REQUIRE(rig.product.screen().mode() == go::Mode::Menu);
    settle(rig, now_ms);
}

// A tap of the pad: the focus moves down a row.
inline void move(Rig& rig, uint32_t& now_ms) {
    rig.tap(now_ms);
    settle(rig, now_ms);
}

// A press of the button: the focused row is acted on, once, and the focus stays on it.
inline void change(Rig& rig, uint32_t& now_ms) {
    rig.press(now_ms);
    rig.run(now_ms, now_ms + 200);
    now_ms += 200;
    settle(rig, now_ms);
}

// The blob goes to flash kSettleMs after the last tap, in the next free window.
inline void run_past_the_write_settle(Rig& rig, uint32_t& now_ms) {
    const uint32_t settle_ms = timing::DurableWriteWindow::kSettleMs + 1000;
    rig.run(now_ms, now_ms + settle_ms);
    now_ms += settle_ms;
}

inline void focus_on(Rig& rig, uint32_t& now_ms, go::MenuRow row) {
    for (int i = 0; i < go::menu_for(go::Page::Radar).row_count; i++) {
        if (rig.product.screen().editor().focus() == row) break;
        move(rig, now_ms);
    }
    REQUIRE(rig.product.screen().editor().focus() == row);
}

inline bool stored_settings(Rig& rig, go::Settings& out) {
    uint8_t blob[64];
    size_t count = 0;
    if (rig.platform.key_value().read("settings", blob, sizeof(blob), count) != Status::Ok)
        return false;
    return go::from_blob(blob, count, out) == Status::Ok;
}

}  // namespace

#endif
