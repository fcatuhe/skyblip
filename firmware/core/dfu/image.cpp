#include "core/dfu/image.h"

#include <cstring>

#include "core/util/little_endian.h"

namespace skyblip::dfu {

namespace {

constexpr uint32_t kImageMagic = 0x96f3b83d;
constexpr uint16_t kTlvMagic = 0x6907;
constexpr uint16_t kProtectedTlvMagic = 0x6908;
constexpr uint16_t kTlvKeyHash = 0x01;
constexpr uint16_t kTlvImageClass = 0x75;
constexpr size_t kTlvInfoBytes = 4;
constexpr size_t kTlvEntryBytes = 4;

size_t past_protected_area(const uint8_t* tlvs, size_t bytes) {
    if (bytes >= kTlvInfoBytes && get_uint16(tlvs) == kProtectedTlvMagic)
        return get_uint16(tlvs + 2);
    return 0;
}

const uint8_t* find_entry(const uint8_t* tlvs, size_t bytes, size_t at, uint16_t magic,
                          uint16_t type, size_t value_bytes) {
    if (tlvs == nullptr || at + kTlvInfoBytes > bytes || get_uint16(tlvs + at) != magic)
        return nullptr;
    const size_t end = at + get_uint16(tlvs + at + 2);
    if (end > bytes) return nullptr;
    for (at += kTlvInfoBytes; at + kTlvEntryBytes <= end;) {
        const uint16_t entry_type = get_uint16(tlvs + at);
        const uint16_t entry_bytes = get_uint16(tlvs + at + 2);
        at += kTlvEntryBytes;
        if (at + entry_bytes > end) return nullptr;
        if (entry_type == type) return entry_bytes == value_bytes ? tlvs + at : nullptr;
        at += entry_bytes;
    }
    return nullptr;
}

}  // namespace

bool read_header(const uint8_t* data, size_t bytes, ImageHeader& out) {
    if (data == nullptr || bytes < kImageHeaderBytes || get_uint32(data) != kImageMagic)
        return false;
    out.header_bytes = get_uint16(data + 8);
    out.protected_tlv_bytes = get_uint16(data + 10);
    out.body_bytes = get_uint32(data + 12);
    out.version.major = data[20];
    out.version.minor = data[21];
    out.version.revision = get_uint16(data + 22);
    out.version.build = get_uint32(data + 24);
    return out.header_bytes >= kImageHeaderBytes;
}

bool find_key_hash(const uint8_t* tlvs, size_t bytes, ports::SigningKeyHash& out) {
    if (tlvs == nullptr) return false;
    const uint8_t* value = find_entry(tlvs, bytes, past_protected_area(tlvs, bytes), kTlvMagic,
                                      kTlvKeyHash, out.size());
    if (value == nullptr) return false;
    std::memcpy(out.data(), value, out.size());
    return true;
}

bool find_image_class(const uint8_t* tlvs, size_t bytes, ImageClass& out) {
    const uint8_t* value =
        find_entry(tlvs, bytes, 0, kProtectedTlvMagic, kTlvImageClass, out.size());
    if (value == nullptr) return false;
    std::memcpy(out.data(), value, out.size());
    return true;
}

}  // namespace skyblip::dfu
