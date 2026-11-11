#ifndef SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_PAIRING_GESTURE_H
#define SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_PAIRING_GESTURE_H

#include <cstdint>

#include "products/common/confirm_gesture.h"

namespace skyblip::bravo {

// INFO: fc 11nov26 one press pairs, armed as a prompt's: lamp showing it, thumb up and still
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

    void arm_once_quiet(bool lamp_shows_it, bool released_once, uint32_t released_ms,
                        uint32_t now_ms) {
        if (!requested_ || armed_ || !lamp_shows_it) return;
        armed_ = settled(now_ms, since_ms_) && (!released_once || settled(now_ms, released_ms));
    }

    void spend() { armed_ = false; }
    bool armed() const { return armed_; }

   private:
    static bool settled(uint32_t now_ms, uint32_t since_ms) {
        return now_ms - since_ms >= common::ConfirmGesture::kDoublePressWindowMs;
    }

    uint32_t since_ms_{0};
    bool requested_{false};
    bool armed_{false};
};

}  // namespace skyblip::bravo

#endif
