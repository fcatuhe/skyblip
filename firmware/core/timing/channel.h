// INFO: fc 15sep26 the hour of EN 300 220-2 V3.3.1 Table 4 band M is the only rule refusing a burst
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

    int8_t dbm() const;
    uint32_t samples() const { return samples_; }

   private:
    int32_t centi_dbm_{static_cast<int32_t>(kSeedDbm) * 100};
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

// Sixty one-minute buckets on a ring that turns by elapsed time, so a bucket that
// fell out of the window contributes nothing without anything having to sweep it.
class AirTime {
   public:
    static constexpr uint32_t kWindowMs = 3600000;
    static constexpr uint8_t kBuckets = 60;
    static constexpr uint32_t kBucketMs = kWindowMs / kBuckets;
    static constexpr uint32_t kLimitPermille = 10;
    static constexpr uint32_t kBudgetMs = kWindowMs / 1000 * kLimitPermille;

    void spend(uint32_t now_ms, uint32_t air_ms);

    uint32_t window_ms(uint32_t now_ms) const;
    uint32_t permille(uint32_t now_ms) const { return window_ms(now_ms) * 1000u / kWindowMs; }
    bool may_spend(uint32_t now_ms, uint32_t air_ms) const {
        return window_ms(now_ms) + air_ms <= kBudgetMs;
    }

    uint32_t bursts() const { return bursts_; }
    uint32_t total_ms() const { return total_ms_; }

   private:
    uint32_t bucket_ms_[kBuckets]{};
    // The bucket now_ms falls in, and the instant it opened. The ring turns by
    // ELAPSED time and every age below is an unsigned difference from that
    // instant, because a bucket numbered now_ms / kBucketMs is not a bucket: that
    // number restarts at zero when the counter wraps, and 2^32 ms is not a whole
    // number of minutes either, so numbered buckets throw the whole hour away at
    // the wrap and let the hour straddling it spend the band's allowance twice
    // (ports/clock.h, rule 1).
    uint32_t head_start_ms_{0};
    uint8_t head_{0};
    bool started_{false};
    uint32_t bursts_{0};
    uint32_t total_ms_{0};
};

}  // namespace skyblip::timing

#endif
