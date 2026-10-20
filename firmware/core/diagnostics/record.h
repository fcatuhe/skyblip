#ifndef SKYBLIP_CORE_DIAGNOSTICS_RECORD_H
#define SKYBLIP_CORE_DIAGNOSTICS_RECORD_H

#include <cstdint>

#include "core/events/stamp.h"
#include "core/util/clamp.h"
#include "core/util/little_endian.h"
#include "core/util/result.h"

namespace skyblip::diagnostics {

constexpr uint32_t kRecordBytes = 24;
constexpr uint32_t kPayloadBytes = 16;
constexpr uint32_t kPayloadOffset = 8;

// INFO: fc 20sep26 a corpus outlives its firmware: a type is retired, never renumbered
enum class Type : uint8_t {
    None = 0,
    Boot = 1,
    Config = 2,
    Gnss = 3,
    Pps = 4,
    Burst = 5,
    Dwell = 6,
    Flight = 7,
    Power = 8,
    Barometer = 9,
    Motion = 10,
    Contact = 11,
    Link = 12,
    Traffic = 13,
    Write = 14,
    Screen = 15,
    Gap = 16,
    End = 17,
    Duty = 18,
    Switch = 19,
    Loop = 20,
};

constexpr uint8_t kHighestType = static_cast<uint8_t>(Type::Loop);

const char* type_name(Type type);

constexpr uint8_t kFlagPhaseValid = 1u << 0;
constexpr uint8_t kFlagUtcDated = 1u << 1;

struct Instant {
    uint32_t at_s{0};
    uint16_t into_ms{0};
    bool phase_valid{false};
    bool utc_dated{false};
};

Instant instant_of(const events::Stamp& stamp, bool utc_dated);

struct Record {
    Type type{Type::None};
    uint8_t flags{0};
    uint16_t into_ms{0};
    uint32_t at_s{0};
    uint8_t payload[kPayloadBytes]{};

    bool flagged(uint8_t bit) const { return (flags & bit) != 0; }
    bool phase_valid() const { return flagged(kFlagPhaseValid); }
    bool utc_dated() const { return flagged(kFlagUtcDated); }
    Instant at() const { return Instant{at_s, into_ms, phase_valid(), utc_dated()}; }
};

Record framed(Type type, const Instant& at);

void encode_record(const Record& record, uint8_t* out);
Status decode_record(const uint8_t* raw, Record& out);

inline void set_flag(uint8_t& flags, uint8_t bit, bool on) {
    if (on) flags |= bit;
}

inline void put_int8(uint8_t* out, int8_t value) { *out = static_cast<uint8_t>(value); }

inline int8_t get_int8(const uint8_t* raw) { return static_cast<int8_t>(*raw); }

constexpr int8_t kSampleOffsetFloorMv = -128;
constexpr int8_t kSampleOffsetCeilingMv = 127;

inline int8_t sample_offset_mv(int32_t sample_mv, int32_t median_mv) {
    const int32_t offset = sample_mv - median_mv;
    if (offset > kSampleOffsetCeilingMv) return kSampleOffsetCeilingMv;
    if (offset < kSampleOffsetFloorMv) return kSampleOffsetFloorMv;
    return static_cast<int8_t>(offset);
}

// INFO: fc 03oct26 a burst's last byte holds the stage margin in 10 us steps, +-1.27 ms
constexpr int32_t kStageMarginStepUs = 10;

inline int8_t stage_margin_steps(int32_t margin_us) {
    const int32_t steps = margin_us / kStageMarginStepUs;
    if (steps > INT8_MAX) return INT8_MAX;
    if (steps < INT8_MIN) return INT8_MIN;
    return static_cast<int8_t>(steps);
}

}  // namespace skyblip::diagnostics

#endif
