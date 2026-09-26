#include "core/settings/blob.h"

#include <cstring>

#include "core/fec/crc.h"

namespace skyblip::settings {

namespace {

uint32_t stored_crc(const uint8_t* in, size_t payload_len) {
    return static_cast<uint32_t>(in[1 + payload_len]) |
           (static_cast<uint32_t>(in[1 + payload_len + 1]) << 8) |
           (static_cast<uint32_t>(in[1 + payload_len + 2]) << 16) |
           (static_cast<uint32_t>(in[1 + payload_len + 3]) << 24);
}

}  // namespace

void seal(uint8_t version, const void* payload, size_t payload_len, uint8_t* out, size_t cap) {
    if (cap < blob_bytes(payload_len)) return;
    out[0] = version;
    std::memcpy(out + 1, payload, payload_len);
    const uint32_t crc = fec::crc32(out, 1 + payload_len);
    out[1 + payload_len + 0] = static_cast<uint8_t>(crc);
    out[1 + payload_len + 1] = static_cast<uint8_t>(crc >> 8);
    out[1 + payload_len + 2] = static_cast<uint8_t>(crc >> 16);
    out[1 + payload_len + 3] = static_cast<uint8_t>(crc >> 24);
}

Status open(const uint8_t* in, size_t len, size_t payload_len, void* payload_out) {
    if (len < blob_bytes(payload_len)) return Status::Crc;
    if (fec::crc32(in, 1 + payload_len) != stored_crc(in, payload_len)) return Status::Crc;
    std::memcpy(payload_out, in + 1, payload_len);
    return Status::Ok;
}

bool sealed(const uint8_t* in, size_t len) {
    if (len < kBlobOverhead) return false;
    const size_t payload_len = len - kBlobOverhead;
    return fec::crc32(in, 1 + payload_len) == stored_crc(in, payload_len);
}

}  // namespace skyblip::settings
