#include "core/events/stamp.h"

namespace skyblip::events {

namespace {
constexpr int64_t kSecondUs = 1000000;
}

Stamp stamp_of(uint64_t at_us, uint64_t pps_edge_us, bool pps_locked, uint32_t now_s) {
    Stamp stamp{};
    stamp.at_s = now_s;
    if (!pps_locked) return stamp;

    const int64_t relative_us = static_cast<int64_t>(at_us) - static_cast<int64_t>(pps_edge_us);
    if (relative_us <= -kStampReachUs || relative_us >= kStampReachUs) return stamp;

    int64_t seconds = relative_us / kSecondUs;
    int64_t into_us = relative_us % kSecondUs;
    if (into_us < 0) {
        seconds--;
        into_us += kSecondUs;
    }
    if (seconds < 0 && static_cast<uint64_t>(-seconds) > now_s) return stamp;

    stamp.at_s = static_cast<uint32_t>(static_cast<int64_t>(now_s) + seconds);
    stamp.into_ms = static_cast<uint16_t>(into_us / 1000);
    stamp.phase_valid = true;
    return stamp;
}

}  // namespace skyblip::events
