#ifndef SKYBLIP_CORE_COMMS_TIMING_REPORT_H
#define SKYBLIP_CORE_COMMS_TIMING_REPORT_H

#include "core/timing/slot_timing_statistics.h"

namespace skyblip::comms {

// INFO: fc 20nov26 a buffer bound only: a frame never outgrows the payload
constexpr int kTimingFrameBytes = 384;

class TimingReport {
   public:
    explicit TimingReport(const timing::SlotTimingStatistics& statistics);

    bool exhausted() const { return at_ >= count_; }

    // INFO: fc 04aug26 asked before the first frame, so a bench never holds a report with a hole
    bool fits(int payload_bytes) const;

    // INFO: fc 20nov26 0 when not one field fits, a refusal, and it advances only on a frame
    int next_frame(int payload_bytes, uint8_t* out, int capacity);

   private:
    enum class Kind : uint8_t { Unsigned, Integer, Buckets };

    struct Field {
        uint32_t key;
        Kind kind;
        int64_t value;
        const uint32_t* buckets;
    };

    static constexpr int kMaxFields = 13;

    void add(uint32_t key, Kind kind, int64_t value, const uint32_t* buckets = nullptr);
    static int field_bytes(const Field& field);

    uint32_t pps_buckets_[timing::SlotTimingStatistics::kBuckets]{};
    uint32_t dwell_buckets_[timing::SlotTimingStatistics::kBuckets]{};
    Field fields_[kMaxFields]{};
    int count_{0};
    int at_{0};
    int part_{0};
};

}  // namespace skyblip::comms

#endif
