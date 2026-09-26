#ifndef SKYBLIP_CORE_DIAG_RECORDER_H
#define SKYBLIP_CORE_DIAG_RECORDER_H

#include <cstdint>

#include "core/diag/payload.h"
#include "core/diag/profile.h"
#include "core/diag/record.h"

namespace skyblip::diag {

class Recorder {
   public:
    // INFO: fc 20sep26 64 slots: ~30 records/s across a sector erase and the direct slot behind it
    static constexpr int kCapacity = 64;

    // INFO: fc 20sep26 seven subjects a second, three dwells and own-ship's own burst
    static constexpr uint32_t kPeriodicRecordsPerSecond = 11;

    static constexpr uint32_t kSecondsPerHour = 3600;
    static constexpr uint32_t kMsPerHour = kSecondsPerHour * 1000;

    static constexpr uint32_t records_per_hour(Profile profile) {
        return profile == Profile::PowerRun
                   ? kPowerRunRecordsPerPass * kMsPerHour / kPowerRunRecordPeriodMs
                   : kPeriodicRecordsPerSecond * kSecondsPerHour;
    }

    void arm(Profile profile = Profile::Full);
    void disarm();
    bool armed() const { return armed_; }
    Profile profile() const { return profile_; }

    bool record(const Record& record);
    bool record(const radio::Entry& entry);

    template <class T>
    bool record(const T& value, const Instant& at) {
        if (!armed_) return false;
        return push(record_of(value, at));
    }

    bool peek(Record& out);
    void commit();

    int queued() const { return count_; }
    uint32_t written() const { return written_; }
    uint32_t dropped() const { return dropped_; }

   private:
    bool push(const Record& record);
    void store(const Record& record);
    void note_drop(const Record& record);
    void flush_gap();
    bool full() const { return count_ == kCapacity; }

    Record ring_[kCapacity]{};
    Instant gap_from_{};
    Instant gap_to_{};
    uint32_t gap_dropped_{0};
    uint32_t written_{0};
    uint32_t dropped_{0};
    int head_{0};
    int count_{0};
    Profile profile_{Profile::Full};
    bool armed_{false};
};

}  // namespace skyblip::diag

#endif
