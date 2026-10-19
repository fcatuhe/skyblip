#include "core/radio/log.h"

namespace skyblip::radio {

uint16_t tx_span_of(uint64_t done_at_us, uint64_t deadline_us) {
    if (done_at_us <= deadline_us) return 0;
    const uint64_t span_us = done_at_us - deadline_us;
    return span_us > kTxSpanCeilingUs ? kTxSpanCeilingUs : static_cast<uint16_t>(span_us);
}

int16_t tx_stage_margin_of(uint64_t staged_at_us, uint64_t deadline_us) {
    const int64_t margin_us =
        static_cast<int64_t>(deadline_us) - static_cast<int64_t>(staged_at_us);
    if (margin_us > INT16_MAX) return INT16_MAX;
    if (margin_us < INT16_MIN) return INT16_MIN;
    return static_cast<int16_t>(margin_us);
}

void Log::record(const Entry& entry) {
    entries_[written_ % kMaxEntries] = entry;
    written_++;
}

const Entry& Log::newest(int i) const {
    const int held = count();
    if (held == 0) return entries_[0];
    const int back = i < 0 ? 0 : (i >= held ? held - 1 : i);
    return entries_[(written_ - 1u - static_cast<uint32_t>(back)) % kMaxEntries];
}

}  // namespace skyblip::radio
