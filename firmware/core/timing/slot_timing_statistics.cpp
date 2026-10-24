#include "core/timing/slot_timing_statistics.h"

#include <cstdint>

#include "core/util/clamp.h"

namespace skyblip::timing {

int SlotTimingStatistics::bucket_of(int64_t error_us) {
    const int64_t magnitude = error_us < 0 ? -error_us : error_us;
    if (magnitude < kRetuneUs) return 3;
    if (magnitude < kHopGuardUs) return error_us < 0 ? 2 : 4;
    if (magnitude < kJitterGuardUs) return error_us < 0 ? 1 : 5;
    return error_us < 0 ? 0 : 6;
}

void SlotTimingStatistics::record_sample(uint32_t* buckets, uint32_t& samples, int32_t& worst_us,
                                         int64_t error_us) {
    buckets[bucket_of(error_us)]++;
    samples++;
    const int64_t magnitude = error_us < 0 ? -error_us : error_us;
    const int64_t worst_magnitude = worst_us < 0 ? -static_cast<int64_t>(worst_us) : worst_us;
    if (magnitude > worst_magnitude) worst_us = clamp_int32(error_us);
}

void SlotTimingStatistics::record_edge(uint64_t edge_us, bool locked) {
    if (!locked) {
        if (previous_edge_valid_) holdover_events_++;
        previous_edge_valid_ = false;
        return;
    }
    if (!previous_edge_valid_) {
        previous_edge_us_ = edge_us;
        previous_edge_valid_ = true;
        return;
    }
    if (edge_us == previous_edge_us_) return;

    const int64_t delta_us =
        static_cast<int64_t>(edge_us) - static_cast<int64_t>(previous_edge_us_);
    previous_edge_us_ = edge_us;
    // A clock that ran backwards or skipped an edge is not describable against
    // the nominal second at all: holdover, so it cannot masquerade as an
    // enormous phase error in the histogram below.
    if (delta_us <= 0 || delta_us > kHoldoverGapUs) {
        holdover_events_++;
        return;
    }
    record_sample(pps_buckets_, pps_samples_, pps_worst_us_, delta_us - kNominalSecondUs);
}

void SlotTimingStatistics::record_dwell_phase(int64_t error_us) {
    record_sample(dwell_buckets_, dwell_samples_, dwell_worst_us_, error_us);
}

void SlotTimingStatistics::record_missed() { missed_++; }
void SlotTimingStatistics::record_refused() { refused_++; }

}  // namespace skyblip::timing
