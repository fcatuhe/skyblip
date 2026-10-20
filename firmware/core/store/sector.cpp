#include "core/store/sector.h"

#include "core/fec/crc.h"
#include "core/util/little_endian.h"

namespace skyblip::store {

namespace {

constexpr uint32_t kChecksummedBytes = kSectorHeaderBytes - 2;

bool owner_known(uint8_t code) {
    return code == static_cast<uint8_t>(SectorOwner::Flights) ||
           code == static_cast<uint8_t>(SectorOwner::Diagnostics);
}

}  // namespace

void encode_sector_header(const SectorHeader& header, uint8_t* out) {
    put_uint16(out + 0, kSectorMagic);
    out[2] = header.version;
    out[3] = static_cast<uint8_t>(header.owner);
    put_uint32(out + 4, header.sequence);
    put_uint32(out + 8, header.session_id);
    // INFO: fc 20sep26 written down, not assumed: a later codec's sectors stay walkable
    out[12] = header.record_bytes;
    out[13] = header.session_start ? kSectorFlagSessionStart : 0;
    put_uint16(out + kChecksummedBytes, fec::crc16_ccitt(out, kChecksummedBytes));
}

Status decode_sector_header(const uint8_t* raw, SectorHeader& out) {
    if (erased(raw, kSectorHeaderBytes)) return Status::Empty;
    if (get_uint16(raw + 0) != kSectorMagic) return Status::Invalid;
    if (fec::crc16_ccitt(raw, kChecksummedBytes) != get_uint16(raw + kChecksummedBytes))
        return Status::Crc;

    out = SectorHeader{};
    out.version = raw[2];
    out.sequence = get_uint32(raw + 4);
    out.session_id = get_uint32(raw + 8);
    out.record_bytes = raw[12];
    out.session_start = (raw[13] & kSectorFlagSessionStart) != 0;
    if (out.version != kSectorVersion || !owner_known(raw[3])) return Status::Unsupported;
    out.owner = static_cast<SectorOwner>(raw[3]);
    return Status::Ok;
}

bool erased(const uint8_t* raw, uint32_t bytes) {
    for (uint32_t i = 0; i < bytes; i++)
        if (raw[i] != 0xFF) return false;
    return true;
}

}  // namespace skyblip::store
