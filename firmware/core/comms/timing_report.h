// core/comms/timing_report.h: the bench's timing answer, cut to fit the link.
#ifndef SKYBLIP_CORE_COMMS_TIMING_REPORT_H
#define SKYBLIP_CORE_COMMS_TIMING_REPORT_H

#include "core/timing/timing_stats.h"

namespace skyblip::comms {

// Enough for every field in one frame at its widest, for the link that can carry
// them all: a frame is never longer than the payload, this only bounds the buffer
// the caller lends.
constexpr int kTimingFrameCap = 384;

class TimingReport {
   public:
    explicit TimingReport(const timing::SlotTimingStats& stats);

    bool exhausted() const { return at_ >= count_; }

    // INFO: fc 04aug26 Whether every field can be placed in some frame of this
    // payload. Asked before the first frame goes out so the answer is all or
    // nothing: a bench left holding two frames of a three-frame report, with no
    // third one coming, has evidence with a hole in it.
    bool fits(int payload) const;

    // The next frame, written into buf. Returns its length, or 0 when the payload
    // is too small to carry a single field - which is a refusal, not a short
    // frame. Advances only when a frame was produced.
    int next_frame(int payload, char* buf, int cap);

   private:
    // One key and its value, measurable before it is written. text != nullptr
    // means a string value.
    struct Field {
        const char* key;
        const char* text;
        long value;
    };

    // Seven counts, comma-joined: cheaper than a JSON array over a FLAT-JSON
    // writer that does not have one, and just as readable on a bench terminal.
    // Room for the widest count a bucket can reach.
    static constexpr int kBucketsTextCap = timing::SlotTimingStats::kBuckets * 11 + 1;
    static constexpr int kMaxFields = 13;

    void add(const char* key, const char* text, long value);
    // "key":value, excluding the comma that joins it to the field before it.
    static int field_bytes(const Field& f);

    char pps_[kBucketsTextCap]{};
    char dwell_[kBucketsTextCap]{};
    Field fields_[kMaxFields]{};
    int count_{0};
    int at_{0};
    int part_{0};
};

}  // namespace skyblip::comms

#endif
