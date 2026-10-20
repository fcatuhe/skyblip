#include "core/comms/timing_report.h"

#include <algorithm>

#include "core/comms/link_registry.h"
#include "core/util/cbor.h"

namespace skyblip::comms {

namespace {

constexpr int kBuckets = timing::SlotTimingStatistics::kBuckets;

int command_bytes() {
    return cbor::unsigned_entry_bytes(link::kCommandKey,
                                      static_cast<uint8_t>(link::Command::Timing));
}

}  // namespace

int TimingReport::field_bytes(const Field& field) {
    switch (field.kind) {
        case Kind::Unsigned:
            return cbor::unsigned_entry_bytes(field.key, static_cast<uint64_t>(field.value));
        case Kind::Integer: return cbor::integer_entry_bytes(field.key, field.value);
        case Kind::Buckets:
            return cbor::unsigned_array_entry_bytes(field.key, field.buckets, kBuckets);
    }
    return 0;
}

void TimingReport::add(uint32_t key, Kind kind, int64_t value, const uint32_t* buckets) {
    if (count_ >= kMaxFields) return;
    fields_[count_++] = Field{key, kind, value, buckets};
}

// pps_us and dwell_us are the two histograms in kBuckets order: the SX1262's own
// retune time, core/timing::kHopGuardMs and core/timing::kJitterGuardMs on each
// side of the centre bucket (core/timing/slot_timing_statistics.h). holdover_events, missed and
// refused are counted apart from both, on purpose: a fault a histogram cannot
// bound must not be folded into one that can.
TimingReport::TimingReport(const timing::SlotTimingStatistics& statistics) {
    for (int i = 0; i < kBuckets; i++) {
        pps_buckets_[i] = statistics.pps_bucket(i);
        dwell_buckets_[i] = statistics.dwell_bucket(i);
    }
    add(link::timing_key::kPpsUs, Kind::Buckets, 0, pps_buckets_);
    add(link::timing_key::kPpsWorstUs, Kind::Integer, statistics.pps_worst_us());
    add(link::timing_key::kPpsSamples, Kind::Unsigned, statistics.pps_samples());
    add(link::timing_key::kDwellUs, Kind::Buckets, 0, dwell_buckets_);
    add(link::timing_key::kDwellWorstUs, Kind::Integer, statistics.dwell_worst_us());
    add(link::timing_key::kDwellSamples, Kind::Unsigned, statistics.dwell_samples());
    add(link::timing_key::kHoldoverEvents, Kind::Unsigned, statistics.holdover_events());
    add(link::timing_key::kMissed, Kind::Unsigned, statistics.missed());
    add(link::timing_key::kRefused, Kind::Unsigned, statistics.refused());
    add(link::timing_key::kSwitchHopUs, Kind::Unsigned, statistics.switch_hop_us());
    add(link::timing_key::kSwitchToOBandUs, Kind::Unsigned, statistics.switch_to_o_band_us());
    add(link::timing_key::kSwitchToMBandUs, Kind::Unsigned, statistics.switch_to_m_band_us());
    add(link::timing_key::kLateDwells, Kind::Unsigned, statistics.late_dwells());
}

bool TimingReport::fits(int payload_bytes) const {
    int widest = 0;
    for (int i = 0; i < count_; i++) widest = std::max(field_bytes(fields_[i]), widest);
    cbor::MapBudget budget(payload_bytes);
    return budget.take(command_bytes()) &&
           budget.take(cbor::unsigned_entry_bytes(link::timing_key::kPart, count_)) &&
           budget.take(cbor::boolean_entry_bytes(link::timing_key::kMore)) && budget.take(widest);
}

int TimingReport::next_frame(int payload_bytes, uint8_t* out, int capacity) {
    if (exhausted()) return 0;

    const int room = std::min(payload_bytes, capacity);
    cbor::MapBudget budget(room);
    budget.take(command_bytes());
    budget.take(cbor::unsigned_entry_bytes(link::timing_key::kPart, part_));
    budget.take(cbor::boolean_entry_bytes(link::timing_key::kMore));
    int packed = 0;
    while (at_ + packed < count_ && budget.take(field_bytes(fields_[at_ + packed]))) packed++;
    if (packed == 0) return 0;

    cbor::Writer writer(out, room);
    writer.add_code(link::kCommandKey, link::Command::Timing);
    writer.add_unsigned(link::timing_key::kPart, part_);
    writer.add_boolean(link::timing_key::kMore, at_ + packed < count_);
    for (int i = 0; i < packed; i++) {
        const Field& field = fields_[at_ + i];
        switch (field.kind) {
            case Kind::Unsigned:
                writer.add_unsigned(field.key, static_cast<uint64_t>(field.value));
                break;
            case Kind::Integer: writer.add_integer(field.key, field.value); break;
            case Kind::Buckets:
                writer.add_unsigned_array(field.key, field.buckets, kBuckets);
                break;
        }
    }
    const int bytes = writer.finish();
    if (bytes == 0) return 0;

    at_ += packed;
    part_++;
    return bytes;
}

}  // namespace skyblip::comms
