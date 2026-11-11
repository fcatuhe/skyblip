#include "products/skyblip_go/services/screen.h"

namespace skyblip::go {

BravoView ScreenService::bravo_view() const {
    BravoView view;
    view.paired = paired(settings_);
    view.paired_tag = events::bluetooth_tag(settings_.bravo);
    view.link = bravo_.link();
    view.step = bravo_.step();
    view.refusal = bravo_.refusal();
    view.found_count = bravo_.found_count();
    for (int i = 0; i < view.found_count; i++)
        view.found_tags[i] = events::bluetooth_tag(bravo_.found(i));
    view.pairing_tag = events::bluetooth_tag(bravo_.pairing_with());
    return view;
}

void ScreenService::act_on_bravo(BravoAction action, uint32_t now_ms) {
    switch (action) {
        case BravoAction::None: return;
        case BravoAction::Redraw: dirty_ = true; return;
        case BravoAction::Leave: show_page(page_after(Page::Bravo)); return;
        case BravoAction::Pair:
            bravo_.pair(bravo_page_.focus(), now_ms);
            dirty_ = true;
            return;
        case BravoAction::Cancel:
        case BravoAction::Retry:
            bravo_.begin_pairing(now_ms);
            bravo_page_.open(now_ms);
            dirty_ = true;
            return;
        case BravoAction::Unpair:
            bravo_.unpair();
            show_page(page_after(Page::Bravo));
            return;
    }
}

void ScreenService::step_bravo(uint32_t now_ms) {
    if (on_bravo_page()) act_on_bravo(bravo_page_.idle(bravo_view(), now_ms), now_ms);
}

// INFO: fc 11nov26 once a boot, never over an alarm, and gone after the menu's idle hold
void ScreenService::sync_notice(uint32_t now_ms) {
    if (notice_ &&
        (alarm_takes_glass() || now_ms - notice_since_ms_ >= MenuEditor::kIdleReturnHoldMs))
        close_notice();
    if (notice_ || alarm_takes_glass()) return;
    if (!bravo_.own_icao_warning().take_notice(settings_.own_icao)) return;
    notice_ = true;
    notice_since_ms_ = now_ms;
    change_screen();
}

bool ScreenService::close_notice() {
    if (!notice_) return false;
    notice_ = false;
    change_screen();
    return true;
}

}  // namespace skyblip::go
