#include "core/comms/timing_report.h"

#include <algorithm>

#include "core/comms/frame_budget.h"
#include "core/util/format.h"
#include "core/util/json.h"

namespace skyblip::comms {

namespace {

int format_buckets(const timing::SlotTimingStats& stats, bool pps, char* out, int capacity) {
    int written = 0;
    for (int i = 0; i < timing::SlotTimingStats::kBuckets && written < capacity - 1; i++) {
        if (i > 0) out[written++] = ',';
        written += format_uint(out + written, pps ? stats.pps_bucket(i) : stats.dwell_bucket(i));
    }
    out[written] = 0;
    return written;
}

}  // namespace

int TimingReport::field_bytes(const Field& field) {
    return field.text != nullptr ? frame::string_field_bytes(field.key, field.text)
                                 : frame::integer_field_bytes(field.key, field.value);
}

void TimingReport::add(const char* key, const char* text, long value) {
    if (count_ >= kMaxFields) return;
    fields_[count_++] = Field{key, text, value};
}

// pps_us and dwell_us are the two histograms in kBuckets order: the SX1262's own
// retune time, core/timing::kHopGuardMs and core/timing::kJitterGuardMs on each
// side of the centre bucket (core/timing/slot_timing_stats.h). holdover, missed and
// refused are counted apart from both, on purpose: a fault a histogram cannot
// bound must not be folded into one that can.
TimingReport::TimingReport(const timing::SlotTimingStats& stats) {
    format_buckets(stats, true, pps_buckets_, kBucketsTextBytes);
    format_buckets(stats, false, dwell_buckets_, kBucketsTextBytes);

    // Every count goes through frame::counter and not through a cast: the worst_us
    // pair is signed on both platforms and stays as it is, but a uint32_t sample
    // count cast straight to long is a negative number on the nRF52 and a correct
    // one in this suite (core/comms/frame_budget.h).
    add("pps_us", pps_buckets_, 0);
    add("pps_worst_us", nullptr, stats.pps_worst_us());
    add("pps_samples", nullptr, frame::counter(stats.pps_samples()));
    add("dwell_us", dwell_buckets_, 0);
    add("dwell_worst_us", nullptr, stats.dwell_worst_us());
    add("dwell_samples", nullptr, frame::counter(stats.dwell_samples()));
    add("holdover", nullptr, frame::counter(stats.holdover_events()));
    add("missed", nullptr, frame::counter(stats.missed()));
    add("refused", nullptr, frame::counter(stats.refused()));
    add("switch_hop_us", nullptr, frame::counter(stats.switch_hop_us()));
    add("switch_to_o_us", nullptr, frame::counter(stats.switch_to_oband_us()));
    add("switch_to_m_us", nullptr, frame::counter(stats.switch_to_mband_us()));
    add("late_dwells", nullptr, frame::counter(stats.late_dwells()));
}

bool TimingReport::fits(int payload_bytes) const {
    int widest = 0;
    for (int i = 0; i < count_; i++) {
        const int bytes = field_bytes(fields_[i]);
        widest = std::max(bytes, widest);
    }
    frame::Budget budget(payload_bytes);
    return budget.take(field_bytes(Field{"cmd", "timing", 0})) &&
           budget.take(field_bytes(Field{"part", nullptr, count_})) &&
           budget.take(frame::kMoreFieldBytes) && budget.take(widest);
}

int TimingReport::next_frame(int payload_bytes, char* out, int capacity) {
    if (exhausted()) return 0;

    int room = payload_bytes + 1;
    room = std::min(room, capacity);

    frame::Budget budget(room - 1);
    budget.take(field_bytes(Field{"cmd", "timing", 0}));
    budget.take(field_bytes(Field{"part", nullptr, part_}));
    budget.take(frame::kMoreFieldBytes);
    int packed = 0;
    while (at_ + packed < count_ && budget.take(field_bytes(fields_[at_ + packed]))) packed++;
    if (packed == 0) return 0;

    json::Writer writer(out, room);
    writer.add_string("cmd", "timing");
    writer.add_integer("part", part_);
    writer.add_boolean("more", at_ + packed < count_);
    for (int i = 0; i < packed; i++) {
        const Field& field = fields_[at_ + i];
        if (field.text != nullptr)
            writer.add_string(field.key, field.text);
        else
            writer.add_integer(field.key, field.value);
    }
    const int bytes = writer.finish();
    if (writer.overflowed()) return 0;

    at_ += packed;
    part_++;
    return bytes;
}

}  // namespace skyblip::comms
