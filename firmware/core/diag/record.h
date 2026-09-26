#ifndef SKYBLIP_CORE_DIAG_RECORD_H
#define SKYBLIP_CORE_DIAG_RECORD_H

#include <cstdint>

#include "core/events/stamp.h"
#include "core/util/result.h"

namespace skyblip::diag {

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
    Baro = 9,
    Motion = 10,
    Contact = 11,
    Link = 12,
    Traffic = 13,
    Write = 14,
    Screen = 15,
    Gap = 16,
    End = 17,
    Duty = 18,
};

constexpr uint8_t kHighestType = static_cast<uint8_t>(Type::Duty);

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

inline void put_u16(uint8_t* out, uint16_t v) {
    out[0] = static_cast<uint8_t>(v);
    out[1] = static_cast<uint8_t>(v >> 8);
}

inline void put_u32(uint8_t* out, uint32_t v) {
    out[0] = static_cast<uint8_t>(v);
    out[1] = static_cast<uint8_t>(v >> 8);
    out[2] = static_cast<uint8_t>(v >> 16);
    out[3] = static_cast<uint8_t>(v >> 24);
}

inline void set_flag(uint8_t& flags, uint8_t bit, bool on) {
    if (on) flags |= bit;
}

inline uint16_t get_u16(const uint8_t* raw) {
    return static_cast<uint16_t>(raw[0] | (static_cast<uint16_t>(raw[1]) << 8));
}

inline uint32_t get_u32(const uint8_t* raw) {
    return static_cast<uint32_t>(raw[0]) | (static_cast<uint32_t>(raw[1]) << 8) |
           (static_cast<uint32_t>(raw[2]) << 16) | (static_cast<uint32_t>(raw[3]) << 24);
}

inline void put_i8(uint8_t* out, int8_t v) { *out = static_cast<uint8_t>(v); }
inline void put_i16(uint8_t* out, int16_t v) { put_u16(out, static_cast<uint16_t>(v)); }
inline void put_i32(uint8_t* out, int32_t v) { put_u32(out, static_cast<uint32_t>(v)); }

inline int8_t get_i8(const uint8_t* raw) { return static_cast<int8_t>(*raw); }
inline int16_t get_i16(const uint8_t* raw) { return static_cast<int16_t>(get_u16(raw)); }
inline int32_t get_i32(const uint8_t* raw) { return static_cast<int32_t>(get_u32(raw)); }

inline uint16_t clamp_u16(uint32_t v) { return v > 0xFFFFu ? 0xFFFFu : static_cast<uint16_t>(v); }

inline int16_t clamp_i16(int32_t v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return static_cast<int16_t>(v);
}

}  // namespace skyblip::diag

#endif
