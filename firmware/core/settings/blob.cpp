#include "core/settings/blob.h"

#include <cstring>

#include "core/fec/crc.h"
#include "core/util/little_endian.h"

namespace skyblip::settings {

namespace {

uint32_t stored_crc(const uint8_t* data, size_t payload_bytes) {
    return get_u32(data + 1 + payload_bytes);
}

}  // namespace

const char* to_string(Fallback fallback) {
    switch (fallback) {
        case Fallback::Prior: return "prior";
        case Fallback::Defaults: return "defaults";
        case Fallback::None: break;
    }
    return "none";
}

void seal(uint8_t version, const void* payload, size_t payload_bytes, uint8_t* out,
          size_t capacity) {
    if (capacity < blob_bytes(payload_bytes)) return;
    out[0] = version;
    std::memcpy(out + 1, payload, payload_bytes);
    put_u32(out + 1 + payload_bytes, fec::crc32(out, 1 + payload_bytes));
}

Status open(const uint8_t* data, size_t bytes, size_t payload_bytes, void* payload_out) {
    if (bytes < blob_bytes(payload_bytes)) return Status::Crc;
    if (fec::crc32(data, 1 + payload_bytes) != stored_crc(data, payload_bytes)) return Status::Crc;
    std::memcpy(payload_out, data + 1, payload_bytes);
    return Status::Ok;
}

bool sealed(const uint8_t* data, size_t bytes) {
    if (bytes < kBlobOverhead) return false;
    const size_t payload_bytes = bytes - kBlobOverhead;
    return fec::crc32(data, 1 + payload_bytes) == stored_crc(data, payload_bytes);
}

}  // namespace skyblip::settings
