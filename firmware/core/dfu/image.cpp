#include "core/dfu/image.h"

#include <cstring>

namespace skyblip::dfu {

namespace {

constexpr uint32_t kImageMagic = 0x96f3b83d;
constexpr uint16_t kTlvMagic = 0x6907;
constexpr uint16_t kProtectedTlvMagic = 0x6908;
constexpr uint16_t kTlvKeyHash = 0x01;
constexpr size_t kTlvInfoBytes = 4;
constexpr size_t kTlvEntryBytes = 4;

uint16_t u16(const uint8_t* in) { return static_cast<uint16_t>(in[0] | (in[1] << 8)); }

uint32_t u32(const uint8_t* in) {
    return static_cast<uint32_t>(u16(in)) | (static_cast<uint32_t>(u16(in + 2)) << 16);
}

size_t past_protected_area(const uint8_t* tlvs, size_t len) {
    if (len >= kTlvInfoBytes && u16(tlvs) == kProtectedTlvMagic) return u16(tlvs + 2);
    return 0;
}

}  // namespace

bool read_header(const uint8_t* bytes, size_t len, ImageHeader& out) {
    if (bytes == nullptr || len < kImageHeaderBytes || u32(bytes) != kImageMagic) return false;
    out.header_bytes = u16(bytes + 8);
    out.body_bytes = u32(bytes + 12);
    out.version.major = bytes[20];
    out.version.minor = bytes[21];
    out.version.revision = u16(bytes + 22);
    out.version.build = u32(bytes + 24);
    return out.header_bytes >= kImageHeaderBytes;
}

bool find_key_hash(const uint8_t* tlvs, size_t len, ports::SigningKeyHash& out) {
    if (tlvs == nullptr) return false;
    size_t at = past_protected_area(tlvs, len);
    if (at + kTlvInfoBytes > len || u16(tlvs + at) != kTlvMagic) return false;
    const size_t end = at + u16(tlvs + at + 2);
    if (end > len) return false;
    for (at += kTlvInfoBytes; at + kTlvEntryBytes <= end;) {
        const uint16_t type = u16(tlvs + at);
        const uint16_t value_bytes = u16(tlvs + at + 2);
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
