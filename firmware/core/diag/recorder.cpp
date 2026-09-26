#include "core/diag/recorder.h"

namespace skyblip::diag {

namespace {

uint32_t span_ms(const Instant& from, const Instant& to) {
    const int64_t seconds = static_cast<int64_t>(to.at_s) - static_cast<int64_t>(from.at_s);
    const int64_t span = seconds * 1000 + to.into_ms - from.into_ms;
    if (span <= 0) return 0;
    return span > 0xFFFFFFFF ? 0xFFFFFFFFu : static_cast<uint32_t>(span);
}

}  // namespace

void Recorder::arm(Profile profile) {
    if (armed_) return;
    head_ = 0;
    count_ = 0;
    written_ = 0;
    dropped_ = 0;
    gap_dropped_ = 0;
    profile_ = profile;
    armed_ = true;
}

void Recorder::disarm() { armed_ = false; }

bool Recorder::record(const Record& record) {
    if (!armed_) return false;
    return push(record);
}

bool Recorder::record(const radio::Entry& entry) {
    if (!armed_) return false;
    return push(record_of(entry));
}

bool Recorder::push(const Record& record) {
    // INFO: fc 21sep26 a type the profile never wanted is not a hole, so it is no drop
    if (!lists(profile_, record.type)) return false;
    flush_gap();
    if (full()) {
        note_drop(record);
        return false;
    }
    store(record);
    return true;
}

void Recorder::store(const Record& record) {
    ring_[(head_ + count_) % kCapacity] = record;
    count_++;
    written_++;
}

void Recorder::note_drop(const Record& record) {
    if (gap_dropped_ == 0) gap_from_ = record.at();
    gap_to_ = record.at();
    gap_dropped_++;
    dropped_++;
}

void Recorder::flush_gap() {
    if (gap_dropped_ == 0 || full()) return;
    Gap gap{};
    gap.dropped = gap_dropped_;
    gap.span_ms = span_ms(gap_from_, gap_to_);
    gap.total = dropped_;
    gap.capacity = static_cast<uint16_t>(kCapacity);
    gap_dropped_ = 0;
    store(record_of(gap, gap_from_));
}

bool Recorder::peek(Record& out) {
    flush_gap();
    if (count_ == 0) return false;
    out = ring_[head_];
    return true;
}

void Recorder::commit() {
    if (count_ == 0) return;
    head_ = (head_ + 1) % kCapacity;
    count_--;
}

}  // namespace skyblip::diag
