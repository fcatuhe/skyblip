#include "core/indication/sign.h"

namespace skyblip::indication {

void SignPlayer::begin(const Sign& sign, uint32_t now_ms) {
    sign_ = &sign;
    started_ms_ = now_ms;
}

Signal SignPlayer::update(uint32_t now_ms) {
    if (!sign_) return Signal{};
    const uint32_t elapsed = now_ms - started_ms_;
    if (elapsed >= length_ms(*sign_)) {
        sign_ = nullptr;
        return Signal{};
    }
    if (elapsed < sign_->lead_ms) return Signal{};
    const uint32_t period = static_cast<uint32_t>(sign_->pulse_ms) + sign_->gap_ms;
    const bool pulsing = (elapsed - sign_->lead_ms) % period < sign_->pulse_ms;
    return Signal{pulsing ? sign_->lamp : Colour::None, pulsing};
}

}  // namespace skyblip::indication
