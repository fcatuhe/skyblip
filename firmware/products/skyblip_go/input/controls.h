#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_INPUT_CONTROLS_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_INPUT_CONTROLS_H

#include <cstdint>

#include "core/events/input.h"
#include "core/power/shutdown.h"

namespace skyblip::go {

enum class Gesture : uint8_t { None = 0, Tap = 1, LongTouch = 2, Press = 3 };

class Controls {
   public:
    static constexpr uint32_t kLongTouchMs = 1000;

    static_assert(kLongTouchMs < power::kLongPressHoldMs,
                  "the pad's long touch has to resolve before the press that stows the device");

    Gesture read(const events::ContactEvent& event) {
        return event.contact == events::Contact::Button ? button(event) : pad(event);
    }

    Gesture tick(uint32_t now_ms) {
        if (!touching_ || touch_spent_ || now_ms - touched_ms_ < kLongTouchMs) return Gesture::None;
        touch_spent_ = true;
        return Gesture::LongTouch;
    }

    bool touching() const { return touching_; }

   private:
    Gesture button(const events::ContactEvent& event) {
        if (event.down) {
            pressing_ = true;
            pressed_ms_ = event.at_ms;
            touch_spent_ = touch_spent_ || touching_;
            return Gesture::None;
        }
        pressing_ = false;
        return released_before_the_stow(event.at_ms) ? Gesture::Press : Gesture::None;
    }

    Gesture pad(const events::ContactEvent& event) {
        if (event.down) {
            touching_ = true;
            touched_ms_ = event.at_ms;
            touch_spent_ = pressing_;
            return Gesture::None;
        }
        touching_ = false;
        if (touch_spent_) return Gesture::None;
        touch_spent_ = true;
        return released_before_the_long_touch(event.at_ms) ? Gesture::Tap : Gesture::LongTouch;
    }

    bool released_before_the_stow(uint32_t at_ms) const {
        return at_ms - pressed_ms_ < power::kLongPressHoldMs;
    }

    bool released_before_the_long_touch(uint32_t at_ms) const {
        return at_ms - touched_ms_ < kLongTouchMs;
    }

    uint32_t pressed_ms_{0};
    uint32_t touched_ms_{0};
    bool pressing_{false};
    bool touching_{false};
    bool touch_spent_{false};
};

}  // namespace skyblip::go

#endif
