#include "core/diagnostics/record.h"

#include "core/store/sector.h"
#include "protocols/common/little_endian.h"

namespace skyblip::diagnostics {

Instant instant_of(const events::Stamp& stamp, bool utc_dated) {
    Instant at{};
    at.at_s = stamp.at_s;
    at.into_ms = stamp.into_ms;
    at.phase_valid = stamp.phase_valid;
    at.utc_dated = utc_dated;
    return at;
}

Record framed(Type type, const Instant& at) {
    Record record{};
    record.type = type;
    record.at_s = at.at_s;
    record.into_ms = at.into_ms;
    if (at.phase_valid) record.flags |= kFlagPhaseValid;
    if (at.utc_dated) record.flags |= kFlagUtcDated;
    return record;
}

void encode_record(const Record& record, uint8_t* out) {
    out[0] = static_cast<uint8_t>(record.type);
    out[1] = record.flags;
    wire::put_uint16(out + 2, record.into_ms);
    wire::put_uint32(out + 4, record.at_s);
    for (uint32_t i = 0; i < kPayloadBytes; i++) out[kPayloadOffset + i] = record.payload[i];
}

Status decode_record(const uint8_t* raw, Record& out) {
    if (store::erased(raw, kRecordBytes)) return Status::Empty;
    if (raw[0] == 0 || raw[0] > kHighestType) return Status::Unsupported;

    out = Record{};
    out.type = static_cast<Type>(raw[0]);
    out.flags = raw[1];
    out.into_ms = wire::get_uint16(raw + 2);
    out.at_s = wire::get_uint32(raw + 4);
    for (uint32_t i = 0; i < kPayloadBytes; i++) out.payload[i] = raw[kPayloadOffset + i];
    return Status::Ok;
}

}  // namespace skyblip::diagnostics
