// INFO: fc 14nov26 how own-ship shares EN 300 220 band M: the hour's duty cycle and polite access
#ifndef SKYBLIP_CORE_TIMING_CHANNEL_H
#define SKYBLIP_CORE_TIMING_CHANNEL_H

#include <cstdint>

#include "protocols/adsl/physical.h"
#include "protocols/adsl/transmit.h"
#include "protocols/common/en300220.h"

namespace skyblip::timing {

// INFO: fc 14nov26 EN 300 220-1 5.21.3.2's declarations, numbered as the clause lists them
struct PoliteAccess {
    // INFO: fc 14nov26 1) the CCA interval, over Table 48's 160 us floor
    static constexpr uint32_t kCcaWindowUs = 200;
    // INFO: fc 14nov26 2) the dead time, CCA end to burst on air, where Table 48 allows 5 ms
    static constexpr uint32_t kDeadTimeCeilingUs = 2000;
    // INFO: fc 14nov26 4) the deferral unit is one ADS-L burst, as 5.21.1's note asks
    static constexpr uint32_t kDeferralUnitUs = adsl::kAirTimeMs * 1000;
    // INFO: fc 14nov26 3) and 5) Transmitter::deferral_draw picks 1 to 8 units at random
    static constexpr uint32_t kDeferralUnits = 8;
    static constexpr uint32_t kDeferralFloorUs = kDeferralUnitUs;
    static constexpr uint32_t kDeferralCeilingUs = kDeferralUnits * kDeferralUnitUs;
    // INFO: fc 14nov26 6) Ton max is one burst, under Table 48's 1 s
    static constexpr uint32_t kTransmissionCeilingUs = adsl::kAirTimeMs * 1000;
    // INFO: fc 14nov26 7) Toff min on one frequency, which the schedule holds (README)
    static constexpr uint32_t kSameFrequencyOffFloorMs = en300220::kSameFrequencyOffFloorMs;
    // INFO: fc 14nov26 a burst starts this long into its dwell, so the CCA listens on its channel
    static constexpr uint32_t kCcaLeadUs = kCcaWindowUs + kDeadTimeCeilingUs;
};

static_assert(PoliteAccess::kCcaWindowUs >= en300220::kCcaIntervalFloorUs &&
                  PoliteAccess::kDeadTimeCeilingUs <= en300220::kDeadTimeCeilingUs &&
                  PoliteAccess::kDeferralFloorUs >= PoliteAccess::kCcaWindowUs &&
                  PoliteAccess::kDeferralFloorUs >= en300220::kDeferralFloorUs &&
                  PoliteAccess::kTransmissionCeilingUs <= en300220::kTransmissionCeilingMs * 1000,
              "EN 300 220-1 Table 48 bounds every declared parameter");

// INFO: fc 14nov26 EN 300 220-1 Table 45 on ADS-L's 200 kHz channel, at the front end's own gain
constexpr int32_t cca_threshold_centi_dbm(int32_t receive_gain_centi_dbd) {
    return en300220::cca_threshold_centi_dbm(adsl::kMBandChannelBandwidthHz,
                                             en300220::kBandMErpCeilingMw, receive_gain_centi_dbd);
}

inline bool channel_clear(int8_t level_dbm, int32_t threshold_centi_dbm) {
    return en300220::channel_clear(int32_t{level_dbm} * 100, threshold_centi_dbm);
}

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
    static constexpr uint32_t kWindowUs = PoliteAccess::kCcaWindowUs;
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
    static constexpr uint32_t kWindowMs = en300220::kDutyCycleWindowMs;
    static constexpr uint8_t kBuckets = 60;
    static constexpr uint32_t kBucketMs = kWindowMs / kBuckets;
    static constexpr uint32_t kLimitPermille = en300220::kBandMDutyCyclePermille;
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
