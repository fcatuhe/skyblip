#ifndef SKYBLIP_CORE_RADIO_LOG_H
#define SKYBLIP_CORE_RADIO_LOG_H

#include <cstdint>

#include "core/events/stamp.h"
#include "core/model/aircraft.h"
#include "core/model/band.h"

namespace skyblip::radio {

enum class Event : uint8_t {
    Transmitted = 0,
    Lost = 1,
    Held = 2,
    Unarmed = 3,
    Received = 4,
    Named = 5,
    BadCrc = 6,
    Unframed = 7,
    Miskeyed = 8,
    Undecoded = 9,
    Unsupported = 10,
    Unattempted = 11
};

struct Entry {
    Event event{Event::Undecoded};
    model::Band band{model::Band::M};
    model::Source source{model::Source::AdslDirect};
    uint32_t addr{0};
    uint32_t at_s{0};
    uint16_t into_ms{0};
    uint16_t tx_keyed_us{0};
    uint16_t tx_span_us{0};
    int16_t tx_stage_margin_us{0};
    int8_t rssi_dbm{0};
    int8_t key_offset_s{0};
    uint8_t channel{0};
    uint8_t len{0};
    bool addr_valid{false};
    bool rssi_valid{false};
    bool utc{false};
    bool airborne{false};
    bool phase_valid{false};
    bool tx_span_valid{false};
    bool callsign{false};
};

// INFO: fc 16sep26 the field's own ceiling: a burst this late is a dwell that already ended
constexpr uint16_t kTxSpanLimitUs = 65535;

uint16_t tx_span_of(uint64_t done_at_us, uint64_t deadline_us);

// INFO: fc 03oct26 positive: the burst was staged before its instant, negative: staging overran it
int16_t tx_stage_margin_of(uint64_t staged_at_us, uint64_t deadline_us);

class Log {
   public:
    static constexpr int kCapacity = 16;

    void record(const Entry& entry);
    void clear() { written_ = 0; }

    int count() const { return written_ < kCapacity ? static_cast<int>(written_) : kCapacity; }
    const Entry& newest(int i) const;

   private:
    Entry entry_[kCapacity]{};
    uint32_t written_{0};
};

}  // namespace skyblip::radio

#endif
