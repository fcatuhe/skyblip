#include "core/dfu/image.h"

#include <cstring>

#include "core/util/little_endian.h"

namespace skyblip::dfu {

namespace {

constexpr uint32_t kImageMagic = 0x96f3b83d;
constexpr uint16_t kTlvMagic = 0x6907;
constexpr uint16_t kProtectedTlvMagic = 0x6908;
constexpr uint16_t kTlvKeyHash = 0x01;
constexpr size_t kTlvInfoBytes = 4;
constexpr size_t kTlvEntryBytes = 4;

size_t past_protected_area(const uint8_t* tlvs, size_t len) {
    if (len >= kTlvInfoBytes && get_u16(tlvs) == kProtectedTlvMagic) return get_u16(tlvs + 2);
    return 0;
}

}  // namespace

bool read_header(const uint8_t* data, size_t bytes, ImageHeader& out) {
    if (data == nullptr || bytes < kImageHeaderBytes || get_u32(data) != kImageMagic) return false;
    out.header_bytes = get_u16(data + 8);
    out.body_bytes = get_u32(data + 12);
    out.version.major = data[20];
    out.version.minor = data[21];
    out.version.revision = get_u16(data + 22);
    out.version.build = get_u32(data + 24);
    return out.header_bytes >= kImageHeaderBytes;
}

bool find_key_hash(const uint8_t* tlvs, size_t bytes, ports::SigningKeyHash& out) {
    if (tlvs == nullptr) return false;
    size_t at = past_protected_area(tlvs, bytes);
    if (at + kTlvInfoBytes > bytes || get_u16(tlvs + at) != kTlvMagic) return false;
    const size_t end = at + get_u16(tlvs + at + 2);
    if (end > bytes) return false;
    for (at += kTlvInfoBytes; at + kTlvEntryBytes <= end;) {
        const uint16_t type = get_u16(tlvs + at);
        const uint16_t value_bytes = get_u16(tlvs + at + 2);
        at += kTlvEntryBytes;
        if (at + value_bytes > end) return false;
        if (type == kTlvKeyHash) {
            if (value_bytes != out.size()) return false;
            std::memcpy(out.data(), tlvs + at, out.size());
            return true;
        }
        at += value_bytes;
    }
    return false;
}

uint32_t key_prefix(const ports::SigningKeyHash& key) {
    return (static_cast<uint32_t>(key[0]) << 24) | (static_cast<uint32_t>(key[1]) << 16) |
           (static_cast<uint32_t>(key[2]) << 8) | key[3];
}

}  // namespace skyblip::dfu
