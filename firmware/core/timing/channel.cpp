#include "core/timing/channel.h"

#include <algorithm>

namespace skyblip::timing {

// Integer arithmetic on hundredths of a dBm. Truncation toward zero would stall
// the average one step short of every target, so a residual that is too small
// to weigh still moves the average by one hundredth.
void NoiseFloor::sample(int8_t rssi_dbm) {
    samples_++;
    const int32_t residual = static_cast<int32_t>(rssi_dbm) * 100 - level_centi_dbm_;
    int32_t step = residual * kWeightPercent / 100;
    if (step == 0) step = residual > 0 ? 1 : (residual < 0 ? -1 : 0);
    level_centi_dbm_ += step;
}

int8_t NoiseFloor::level_dbm() const {
    const int32_t rounded =
        level_centi_dbm_ >= 0 ? (level_centi_dbm_ + 50) / 100 : (level_centi_dbm_ - 50) / 100;
    if (rounded < -128) return -128;
    if (rounded > 127) return 127;
    return static_cast<int8_t>(rounded);
}

namespace {
// 10^(-d/10) in units of 1/2^20, for d decibels below the loudest reading in
// the window. A mean of at most kMaxSamples terms cannot be pulled a whole
// decibel by anything further down than the last entry, so it contributes zero.
constexpr uint32_t kPowerRatioQ20[] = {1048576, 832914, 661607, 525533, 417446, 331589,
                                       263390,  209218, 166188, 132008, 104858, 83291,
                                       66161,   52553,  41745,  33159,  26339};
constexpr int kPowerRatioSpanDb = static_cast<int>(sizeof(kPowerRatioQ20) / sizeof(uint32_t));
}  // namespace

int8_t ChannelLevel::mean_dbm(const int8_t* samples, uint8_t count) {
    if (samples == nullptr || count == 0) return 0;
    count = std::min(count, kMaxSamples);
    int8_t peak = samples[0];
    for (uint8_t i = 1; i < count; i++) peak = std::max(samples[i], peak);
    uint32_t sum = 0;
    for (uint8_t i = 0; i < count; i++) {
        const int down = static_cast<int>(peak) - static_cast<int>(samples[i]);
        if (down < kPowerRatioSpanDb) sum += kPowerRatioQ20[down];
    }
    const uint32_t mean = sum / count;
    int down = 0;
    while (down + 1 < kPowerRatioSpanDb && kPowerRatioQ20[down + 1] >= mean) down++;
    return static_cast<int8_t>(static_cast<int>(peak) - down);
}

}  // namespace skyblip::timing
