#include "core/dfu/update.h"

#include <cstring>

#include "core/util/format.h"
#include "core/util/little_endian.h"

namespace skyblip::dfu {

namespace {

void put_version(uint8_t* out, const ports::ImageVersion& version) {
    out[0] = version.major;
    out[1] = version.minor;
    put_uint16(out + 2, version.revision);
    put_uint32(out + 4, version.build);
}

const char* parse_field(const char* at, uint32_t ceiling, char terminator, uint32_t& out) {
    if (*at < '0' || *at > '9') return nullptr;
    uint64_t value = 0;
    for (; *at >= '0' && *at <= '9'; at++) {
        value = value * 10 + static_cast<uint64_t>(*at - '0');
        if (value > ceiling) return nullptr;
    }
    if (*at != terminator) return nullptr;
    out = static_cast<uint32_t>(value);
    return terminator == 0 ? at : at + 1;
}

bool within_widest(const ports::ImageVersion& version) {
    return version.major <= kWidestVersion.major && version.minor <= kWidestVersion.minor &&
           version.revision <= kWidestVersion.revision && version.build <= kWidestVersion.build;
}

bool is_image(const SlotImage& slot, const ports::ImageVersion& version,
              const ports::ImageHash& hash, bool hashed) {
    if (hashed && slot.hashed) return slot.hash == hash;
    return slot.version == version;
}

ports::ImageVersion get_version(const uint8_t* bytes) {
    ports::ImageVersion version;
    version.major = bytes[0];
    version.minor = bytes[1];
    version.revision = get_uint16(bytes + 2);
    version.build = get_uint32(bytes + 4);
    return version;
}

}  // namespace

size_t to_blob(const UpdateRecord& record, uint8_t* out, size_t capacity) {
    const size_t record_bytes = record.hashed ? kUpdateRecordBytes : kVersionsOnlyRecordBytes;
    if (capacity < record_bytes) return 0;
    put_version(out, record.from);
    put_version(out + 8, record.to);
    if (!record.hashed) return record_bytes;
    std::memcpy(out + kVersionsOnlyRecordBytes, record.from_hash.bytes, ports::ImageHash::kBytes);
    std::memcpy(out + kVersionsOnlyRecordBytes + ports::ImageHash::kBytes, record.to_hash.bytes,
                ports::ImageHash::kBytes);
    return kUpdateRecordBytes;
}

bool from_blob(const uint8_t* blob, size_t bytes, UpdateRecord& out) {
    if (bytes != kVersionsOnlyRecordBytes && bytes != kUpdateRecordBytes) return false;
    out = UpdateRecord{};
    out.from = get_version(blob);
    out.to = get_version(blob + 8);
    if (bytes == kVersionsOnlyRecordBytes) return true;
    std::memcpy(out.from_hash.bytes, blob + kVersionsOnlyRecordBytes, ports::ImageHash::kBytes);
    std::memcpy(out.to_hash.bytes, blob + kVersionsOnlyRecordBytes + ports::ImageHash::kBytes,
                ports::ImageHash::kBytes);
    out.hashed = true;
    return true;
}

// INFO: fc 03oct26 a revert swaps the image back into slot 1, a bootloader refusal scrambles it
Outcome outcome(const UpdateRecord& record, const SlotImage& running,
                const std::optional<SlotImage>& staged) {
    if (is_image(running, record.to, record.to_hash, record.hashed)) return Outcome::Landed;
    if (!is_image(running, record.from, record.from_hash, record.hashed)) return Outcome::Unrelated;
    const bool swapped_back = staged && is_image(*staged, record.to, record.to_hash, record.hashed);
    return swapped_back ? Outcome::Reverted : Outcome::Refused;
}

int compare(const ports::ImageVersion& left, const ports::ImageVersion& right) {
    if (left.major != right.major) return left.major < right.major ? -1 : 1;
    if (left.minor != right.minor) return left.minor < right.minor ? -1 : 1;
    if (left.revision != right.revision) return left.revision < right.revision ? -1 : 1;
    if (left.build != right.build) return left.build < right.build ? -1 : 1;
    return 0;
}

const char* version_refusal(const ports::ImageVersion& running, const ports::ImageVersion& incoming,
                            bool downgrade_allowed) {
    if (!downgrade_allowed && compare(incoming, running) < 0) return "older";
    return nullptr;
}

int format_version(const ports::ImageVersion& version, char* out, size_t capacity) {
    if (capacity < kVersionTextBytes || !within_widest(version)) {
        if (capacity > 0) out[0] = 0;
        return 0;
    }
    int written = format_unsigned(out, version.major);
    out[written++] = '.';
    written += format_unsigned(out + written, version.minor);
    out[written++] = '.';
    written += format_unsigned(out + written, version.revision);
    out[written++] = '+';
    written += format_unsigned(out + written, version.build);
    out[written] = 0;
    return written;
}

bool parse_version(const char* text, ports::ImageVersion& out) {
    uint32_t major = 0, minor = 0, revision = 0, build = 0;
    const char* at = parse_field(text, kWidestVersion.major, '.', major);
    if (at) at = parse_field(at, kWidestVersion.minor, '.', minor);
    if (at) at = parse_field(at, kWidestVersion.revision, '+', revision);
    if (at) at = parse_field(at, kWidestVersion.build, 0, build);
    if (!at) return false;
    out.major = static_cast<uint8_t>(major);
    out.minor = static_cast<uint8_t>(minor);
    out.revision = static_cast<uint16_t>(revision);
    out.build = build;
    return true;
}

}  // namespace skyblip::dfu
