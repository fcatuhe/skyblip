#ifndef SKYBLIP_PRODUCTS_COMMON_PROMPT_ANSWER_H
#define SKYBLIP_PRODUCTS_COMMON_PROMPT_ANSWER_H

#include <cstdint>

#include "core/events/input.h"
#include "products/common/confirm_gesture.h"

namespace skyblip::common {

// INFO: fc 11nov26 a down that began before the gesture armed disarms it for a quiet window
class PromptAnswer {
   public:
    void ask(uint32_t now_ms) {
        asked_ms_ = now_ms;
        confirm_.disarm();
    }

    void contact(const events::ContactEvent& event, uint32_t now_ms) {
        if (event.contact != events::Contact::Button) return;
        pressing_ = event.down;
        if (event.down) {
            pressed_ms_ = event.at_ms;
            if (!went_down_after(armed_ms_, now_ms)) confirm_.disarm();
            return;
        }
        released_ms_ = now_ms;
        released_once_ = true;
    }

    void arm_once_quiet(bool readable, uint32_t now_ms) {
        if (confirm_.armed() || !readable || !settled(now_ms)) return;
        if (!settled_for_a_double_press(now_ms, asked_ms_)) return;
        confirm_.arm(now_ms);
        armed_ms_ = now_ms;
    }

    Answer press(uint32_t now_ms) { return confirm_.press(now_ms); }

    Answer tick(uint32_t now_ms) { return confirm_.tick(now_ms); }
    void disarm() { confirm_.disarm(); }
    bool armed() const { return confirm_.armed(); }

    // INFO: fc 11nov26 every gesture arms only once the button has been up a double-press window
    bool settled(uint32_t now_ms) const {
        return !pressing_ && (!released_once_ || settled_for_a_double_press(now_ms, released_ms_));
    }

    // INFO: fc 11nov26 a down is dated at its first sample, a pass or more before it is published
    bool went_down_after(uint32_t since_ms, uint32_t now_ms) const {
        return now_ms - pressed_ms_ < now_ms - since_ms;
    }

    bool pressing() const { return pressing_; }
    uint32_t pressed_ms() const { return pressed_ms_; }
    uint32_t released_ms() const { return released_ms_; }
    bool released_once() const { return released_once_; }

   private:
    static bool settled_for_a_double_press(uint32_t now_ms, uint32_t since_ms) {
        return now_ms - since_ms >= ConfirmGesture::kDoublePressWindowMs;
    }

    ConfirmGesture confirm_{};
    uint32_t asked_ms_{0};
    uint32_t armed_ms_{0};
    uint32_t pressed_ms_{0};
    uint32_t released_ms_{0};
    bool pressing_{false};
    bool released_once_{false};
};

}  // namespace skyblip::common

#endif
