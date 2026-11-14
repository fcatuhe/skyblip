#ifndef SKYBLIP_PRODUCTS_COMMON_PROMPT_ANSWER_H
#define SKYBLIP_PRODUCTS_COMMON_PROMPT_ANSWER_H

#include <cstdint>

#include "core/events/input.h"

namespace skyblip::common {

// INFO: fc 14nov26 one press accepts, and only a press whose down came after the arm on a quiet
// button
class PromptAnswer {
   public:
    // INFO: fc 14nov26 wider than the gap inside a quick run of presses, far narrower than
    // kLongPressHoldMs
    static constexpr uint32_t kAnswerSettleMs = 600;

    void ask(uint32_t now_ms) {
        asked_ms_ = now_ms;
        armed_ = false;
    }

    void contact(const events::ContactEvent& event, uint32_t now_ms) {
        if (event.contact != events::Contact::Button) return;
        pressing_ = event.down;
        if (event.down) {
            pressed_ms_ = event.at_ms;
            if (!went_down_after(armed_ms_, now_ms)) armed_ = false;
            return;
        }
        released_ms_ = now_ms;
        released_once_ = true;
    }

    void arm_once_quiet(bool readable, uint32_t now_ms) {
        if (armed_ || !readable || !settled(now_ms)) return;
        if (!settled_since(now_ms, asked_ms_)) return;
        armed_ = true;
        armed_ms_ = now_ms;
    }

    bool press() {
        if (!armed_) return false;
        armed_ = false;
        return true;
    }

    void disarm() { armed_ = false; }
    bool armed() const { return armed_; }

    bool settled(uint32_t now_ms) const {
        return !pressing_ && (!released_once_ || settled_since(now_ms, released_ms_));
    }

    // INFO: fc 11nov26 a down is dated at its first sample, a pass or more before it is published
    bool went_down_after(uint32_t since_ms, uint32_t now_ms) const {
        return now_ms - pressed_ms_ < now_ms - since_ms;
    }

    static bool settled_since(uint32_t now_ms, uint32_t since_ms) {
        return now_ms - since_ms >= kAnswerSettleMs;
    }

    bool pressing() const { return pressing_; }
    uint32_t pressed_ms() const { return pressed_ms_; }
    uint32_t released_ms() const { return released_ms_; }
    bool released_once() const { return released_once_; }

   private:
    uint32_t asked_ms_{0};
    uint32_t armed_ms_{0};
    uint32_t pressed_ms_{0};
    uint32_t released_ms_{0};
    bool pressing_{false};
    bool released_once_{false};
    bool armed_{false};
};

}  // namespace skyblip::common

#endif
