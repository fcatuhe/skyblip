#include "core/dfu/image.h"

#include <algorithm>
#include <cstring>

#include "protocols/common/little_endian.h"

namespace skyblip::dfu {

namespace {

constexpr uint32_t kImageMagic = 0x96f3b83d;
constexpr uint16_t kTlvMagic = 0x6907;
constexpr uint16_t kProtectedTlvMagic = 0x6908;
constexpr uint16_t kTlvKeyHash = 0x01;
constexpr uint16_t kTlvImageDigest = 0x10;
constexpr uint16_t kTlvImageClass = 0x75;
constexpr size_t kTlvInfoBytes = 4;
constexpr size_t kTlvEntryBytes = 4;
constexpr size_t kTlvReadBytes = 256;
constexpr size_t kHashChunkBytes = 256;

size_t past_protected_area(const uint8_t* tlvs, size_t bytes) {
    if (bytes >= kTlvInfoBytes && wire::get_uint16(tlvs) == kProtectedTlvMagic)
        return wire::get_uint16(tlvs + 2);
    return 0;
}

const uint8_t* find_entry(const uint8_t* tlvs, size_t bytes, size_t at, uint16_t magic,
                          uint16_t type, size_t value_bytes) {
    if (tlvs == nullptr || at + kTlvInfoBytes > bytes || wire::get_uint16(tlvs + at) != magic)
        return nullptr;
    const size_t end = at + wire::get_uint16(tlvs + at + 2);
    if (end > bytes) return nullptr;
    for (at += kTlvInfoBytes; at + kTlvEntryBytes <= end;) {
        const uint16_t entry_type = wire::get_uint16(tlvs + at);
        const uint16_t entry_bytes = wire::get_uint16(tlvs + at + 2);
        at += kTlvEntryBytes;
        if (at + entry_bytes > end) return nullptr;
        if (entry_type == type) return entry_bytes == value_bytes ? tlvs + at : nullptr;
        at += entry_bytes;
    }
    return nullptr;
}

}  // namespace

bool read_header(const uint8_t* data, size_t bytes, ImageHeader& out) {
    if (data == nullptr || bytes < kImageHeaderBytes || wire::get_uint32(data) != kImageMagic)
        return false;
    out.header_bytes = wire::get_uint16(data + 8);
    out.protected_tlv_bytes = wire::get_uint16(data + 10);
    out.body_bytes = wire::get_uint32(data + 12);
    out.version.major = data[20];
    out.version.minor = data[21];
    out.version.revision = wire::get_uint16(data + 22);
    out.version.build = wire::get_uint32(data + 24);
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

bool find_image_digest(const uint8_t* tlvs, size_t bytes, Sha256::Digest& out) {
    if (tlvs == nullptr) return false;
    const uint8_t* value = find_entry(tlvs, bytes, past_protected_area(tlvs, bytes), kTlvMagic,
                                      kTlvImageDigest, out.size());
    if (value == nullptr) return false;
    std::memcpy(out.data(), value, out.size());
    return true;
}

ports::StagedCheck check_image(ImageReader& image) {
    uint8_t header_bytes[kImageHeaderBytes];
    if (image.bytes() < kImageHeaderBytes || !image.read(0, header_bytes, sizeof(header_bytes)))
        return ports::StagedCheck::Unreadable;
    ImageHeader header;
    if (!read_header(header_bytes, sizeof(header_bytes), header))
        return ports::StagedCheck::Damaged;
    const uint32_t hashed = header.tlv_offset() + header.protected_tlv_bytes;
    if (header.tlv_offset() >= image.bytes() || hashed >= image.bytes())
        return ports::StagedCheck::Damaged;

    uint8_t tlvs[kTlvReadBytes];
    const size_t tlv_bytes = std::min<size_t>(sizeof(tlvs), image.bytes() - header.tlv_offset());
    if (!image.read(header.tlv_offset(), tlvs, tlv_bytes)) return ports::StagedCheck::Unreadable;
    Sha256::Digest expected;
    if (!find_image_digest(tlvs, tlv_bytes, expected)) return ports::StagedCheck::Undigested;

    Sha256 digest;
    uint8_t chunk[kHashChunkBytes];
    for (uint32_t at = 0; at < hashed;) {
        const size_t bytes = std::min<size_t>(sizeof(chunk), hashed - at);
        if (!image.read(at, chunk, bytes)) return ports::StagedCheck::Unreadable;
        digest.update(chunk, bytes);
        at += static_cast<uint32_t>(bytes);
    }
    return digest.finish() == expected ? ports::StagedCheck::Matched : ports::StagedCheck::Damaged;
}

}  // namespace skyblip::dfu
