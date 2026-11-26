#include "products/skyblip_bravo/detection_threshold.h"

#include <algorithm>

namespace skyblip::bravo {

uint32_t DetectionThreshold::impossible_of(
    const std::array<uint32_t, adsb::kDownlinkFormats>& formats) {
    uint32_t impossible = 0;
    for (const uint8_t format : kImpossibleFormats) impossible += formats[format];
    return impossible;
}

bool DetectionThreshold::update(uint32_t impossible_frames, uint32_t now_ms) {
    if (!started_) {
        started_ = true;
        window_started_ms_ = now_ms;
        window_impossible_ = impossible_frames;
        return false;
    }
    const uint32_t elapsed_ms = now_ms - window_started_ms_;
    if (elapsed_ms < kFalseAlarmWindowMs) return false;
    const uint64_t noise = static_cast<uint64_t>(impossible_frames - window_impossible_) *
                           kFormatCodes / kImpossibleFormats.size();
    false_alarm_hz_ = static_cast<uint32_t>(noise * 1000 / elapsed_ms);
    window_started_ms_ = now_ms;
    window_impossible_ = impossible_frames;

    int16_t wanted = threshold_dbm_;
    if (false_alarm_hz_ > kFloodFactor * kFalseAlarmCeilingHz)
        wanted = static_cast<int16_t>(threshold_dbm_ + kFloodStepDb);
    else if (false_alarm_hz_ > kFalseAlarmCeilingHz)
        wanted = static_cast<int16_t>(threshold_dbm_ + kStepDb);
    else if (false_alarm_hz_ < kFalseAlarmFloorHz)
        wanted = static_cast<int16_t>(threshold_dbm_ - kStepDb);
    wanted = std::clamp(wanted, kFloorDbm, kCeilingDbm);
    if (wanted == threshold_dbm_) return false;
    threshold_dbm_ = wanted;
    return true;
}

}  // namespace skyblip::bravo
