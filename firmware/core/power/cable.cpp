#include "core/power/cable.h"

namespace skyblip::power {

bool CableWatch::gone(uint32_t now_ms, bool external_power, bool airborne) {
    if (external_power) seen_ = true;
    if (external_power || !seen_ || airborne) {
        gone_since_ms_.reset();
        return false;
    }
    if (!gone_since_ms_) gone_since_ms_ = now_ms;
    return now_ms - *gone_since_ms_ >= kCableGoneHoldMs;
}

std::optional<uint32_t> CableWatch::off_in_ms(uint32_t now_ms) const {
    if (!gone_since_ms_) return std::nullopt;
    const uint32_t gone_ms = now_ms - *gone_since_ms_;
    return gone_ms >= kCableGoneHoldMs ? 0 : kCableGoneHoldMs - gone_ms;
}

}  // namespace skyblip::power
