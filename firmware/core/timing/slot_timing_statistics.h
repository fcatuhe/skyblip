// INFO: fc 20nov26 the bench's slot timing against §C.5, from latched edges and dwell outcomes
#ifndef SKYBLIP_CORE_TIMING_SLOT_TIMING_STATISTICS_H
#define SKYBLIP_CORE_TIMING_SLOT_TIMING_STATISTICS_H

#include <cstdint>

#include "core/timing/slot.h"

namespace skyblip::timing {

// INFO: fc 20nov26 bucket edges are TS_HOP, the switch margin and the retune, and outliers saturate
class SlotTimingStatistics {
   public:
    static constexpr int32_t kRetuneUs = 30;
    static constexpr int32_t kSwitchMarginUs = kSwitchMarginMs * 1000;
    static constexpr int32_t kSwitchRetuneUs = kSwitchRetuneMs * 1000;
    static constexpr int kBuckets = 7;

    // INFO: fc 20nov26 under two seconds and over any jitter, so a gap this wide lost an edge
    static constexpr int64_t kHoldoverGapUs = 1500000;
    static constexpr int64_t kNominalSecondUs = 1000000;

    // INFO: fc 20nov26 safe every pass: only a new edge or a change of lock moves anything
    void record_edge(uint64_t edge_us, bool locked);

    // INFO: fc 28sep26 armed instant to the burst keying, both absolute, so no phase and no wrap
    void record_dwell_phase(int64_t error_us);

    void record_missed();
    // INFO: fc 20nov26 a burst Transmitter::Attempt::over_budget kept off the air
    void record_refused();

    void record_switching(uint32_t hop_us, uint32_t to_o_band_us, uint32_t to_m_band_us,
                          uint32_t late) {
        switch_hop_us_ = hop_us;
        switch_to_o_band_us_ = to_o_band_us;
        switch_to_m_band_us_ = to_m_band_us;
        late_dwells_ = late;
    }

    uint32_t pps_bucket(int index) const { return bucket_at(pps_buckets_, index); }
    uint32_t dwell_bucket(int index) const { return bucket_at(dwell_buckets_, index); }
    uint32_t pps_samples() const { return pps_samples_; }
    uint32_t dwell_samples() const { return dwell_samples_; }
    int32_t pps_worst_us() const { return pps_worst_us_; }
    int32_t dwell_worst_us() const { return dwell_worst_us_; }
    uint32_t holdover_events() const { return holdover_events_; }
    uint32_t missed() const { return missed_; }
    uint32_t refused() const { return refused_; }
    uint32_t switch_hop_us() const { return switch_hop_us_; }
    uint32_t switch_to_o_band_us() const { return switch_to_o_band_us_; }
    uint32_t switch_to_m_band_us() const { return switch_to_m_band_us_; }
    uint32_t late_dwells() const { return late_dwells_; }

    static int bucket_of(int64_t error_us);

   private:
    static uint32_t bucket_at(const uint32_t* buckets, int index) {
        return (index >= 0 && index < kBuckets) ? buckets[static_cast<unsigned>(index)] : 0;
    }
    static void record_sample(uint32_t* buckets, uint32_t& samples, int32_t& worst_us,
                              int64_t error_us);

    uint32_t pps_buckets_[kBuckets]{};
    uint32_t dwell_buckets_[kBuckets]{};
    uint32_t pps_samples_{0};
    uint32_t dwell_samples_{0};
    int32_t pps_worst_us_{0};
    int32_t dwell_worst_us_{0};
    uint32_t holdover_events_{0};
    uint32_t missed_{0};
    uint32_t refused_{0};
    uint32_t switch_hop_us_{0};
    uint32_t switch_to_o_band_us_{0};
    uint32_t switch_to_m_band_us_{0};
    uint32_t late_dwells_{0};
    uint64_t previous_edge_us_{0};
    bool previous_edge_valid_{false};
};

}  // namespace skyblip::timing

#endif
