#include "core/diag/record.h"

#include "core/store/sector.h"

namespace skyblip::diag {

const char* type_name(Type type) {
    switch (type) {
        case Type::None: return "none";
        case Type::Boot: return "boot";
        case Type::Config: return "config";
        case Type::Gnss: return "gnss";
        case Type::Pps: return "pps";
        case Type::Burst: return "burst";
        case Type::Dwell: return "dwell";
        case Type::Flight: return "flight";
        case Type::Power: return "power";
        case Type::Baro: return "baro";
        case Type::Motion: return "motion";
        case Type::Contact: return "contact";
        case Type::Link: return "link";
        case Type::Traffic: return "traffic";
        case Type::Write: return "write";
        case Type::Screen: return "screen";
        case Type::Gap: return "gap";
        case Type::End: return "end";
        case Type::Duty: return "duty";
    }
    return "?";
}

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
    put_u16(out + 2, record.into_ms);
    put_u32(out + 4, record.at_s);
    for (uint32_t i = 0; i < kPayloadBytes; i++) out[kPayloadOffset + i] = record.payload[i];
}

Status decode_record(const uint8_t* raw, Record& out) {
    if (store::erased(raw, kRecordBytes)) return Status::Empty;
    if (raw[0] == 0 || raw[0] > kHighestType) return Status::Unsupported;

    out = Record{};
    out.type = static_cast<Type>(raw[0]);
    out.flags = raw[1];
    out.into_ms = get_u16(raw + 2);
    out.at_s = get_u32(raw + 4);
    for (uint32_t i = 0; i < kPayloadBytes; i++) out.payload[i] = raw[kPayloadOffset + i];
    return Status::Ok;
}

}  // namespace skyblip::diag
