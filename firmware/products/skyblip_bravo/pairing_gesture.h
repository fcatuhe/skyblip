#ifndef SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_PAIRING_GESTURE_H
#define SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_PAIRING_GESTURE_H

#include <cstdint>

#include "products/common/confirm_gesture.h"
#include "products/common/prompt_answer.h"

namespace skyblip::bravo {

// INFO: fc 11nov26 one press pairs, armed by the prompt's rule (products/common/README.md)
class PairingGesture {
   public:
    // INFO: fc 11nov26 an advisory or a prompt that takes the lamp disarms it
    void sync(bool requested, bool lamp_shows_it, uint32_t now_ms) {
        if (armed_ && !lamp_shows_it) armed_ = false;
        if (requested == requested_) return;
        requested_ = requested;
        since_ms_ = now_ms;
        armed_ = false;
    }

    // INFO: fc 11nov26 a down that began at or before the arm instant disarms it, as a prompt's
    void contact(const common::PromptAnswer& answer, uint32_t now_ms) {
        if (armed_ && answer.pressing() && !answer.went_down_after(armed_ms_, now_ms))
            armed_ = false;
    }

    void arm_once_quiet(const common::PromptAnswer& answer, bool lamp_shows_it, uint32_t now_ms) {
        if (!requested_ || armed_ || !lamp_shows_it || !answer.settled(now_ms)) return;
        if (now_ms - since_ms_ < common::ConfirmGesture::kDoublePressWindowMs) return;
        armed_ = true;
        armed_ms_ = now_ms;
    }

    void spend() { armed_ = false; }
    bool armed() const { return armed_; }

   private:
    uint32_t since_ms_{0};
    uint32_t armed_ms_{0};
    bool requested_{false};
    bool armed_{false};
};

}  // namespace skyblip::bravo

#endif
