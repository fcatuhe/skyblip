#ifndef SKYBLIP_CORE_TIMING_CHANNEL_H
#define SKYBLIP_CORE_TIMING_CHANNEL_H

#include <cstdint>

namespace skyblip::timing {

class NoiseFloor {
   public:
    // INFO: fc 15sep26 OGN's seed and weight on the same silicon, oss/nrf52-ogn-tracker:77-78
    static constexpr int8_t kSeedDbm = -105;
    static constexpr int32_t kWeightPercent = 5;

    void sample(int8_t rssi_dbm);

    int8_t level_dbm() const;
    uint32_t samples() const { return samples_; }

   private:
    int32_t level_centi_dbm_{static_cast<int32_t>(kSeedDbm) * 100};
    uint32_t samples_{0};
};

// INFO: fc 15sep26 GetRssiInst is an instant (DS 13.5.2), so a level is a run of reads, meaned
class ChannelLevel {
   public:
    static constexpr uint32_t kWindowUs = 160;
    static constexpr uint8_t kSamples = 9;
    static constexpr uint32_t kSampleSpacingUs = kWindowUs / (kSamples - 1);
    static constexpr uint8_t kMaxSamples = 16;
    static_assert(kSamples >= 2 && kSamples <= kMaxSamples, "a window is at least two readings");
    static_assert(kSampleSpacingUs * (kSamples - 1) >= kWindowUs,
                  "the window is shorter than the interval the figure came from");

    static int8_t mean_dbm(const int8_t* samples, uint8_t count);
};

}  // namespace skyblip::timing

#endif
