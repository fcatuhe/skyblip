#include "core/dfu/update.h"

#include <cstring>

#include "core/util/format.h"

namespace skyblip::dfu {

const char* to_string(ImageState state) {
    switch (state) {
        case ImageState::Confirmed: return "confirmed";
        case ImageState::Probation: return "probation";
        case ImageState::Reverted: return "reverted";
        case ImageState::Refused: return "refused";
    }
    return "confirmed";
}

namespace {

void put_u16(uint8_t* out, uint16_t v) {
    out[0] = static_cast<uint8_t>(v);
    out[1] = static_cast<uint8_t>(v >> 8);
}

void put_u32(uint8_t* out, uint32_t v) {
    put_u16(out, static_cast<uint16_t>(v));
    put_u16(out + 2, static_cast<uint16_t>(v >> 16));
}

uint16_t get_u16(const uint8_t* in) { return static_cast<uint16_t>(in[0] | (in[1] << 8)); }

uint32_t get_u32(const uint8_t* in) {
    return static_cast<uint32_t>(get_u16(in)) | (static_cast<uint32_t>(get_u16(in + 2)) << 16);
}

void put_version(uint8_t* out, const ports::ImageVersion& v) {
    out[0] = v.major;
    out[1] = v.minor;
    put_u16(out + 2, v.revision);
    put_u32(out + 4, v.build);
}

const char* parse_field(const char* at, uint32_t max, char end, uint32_t& out) {
    if (*at < '0' || *at > '9') return nullptr;
    uint64_t value = 0;
    for (; *at >= '0' && *at <= '9'; at++) {
        value = value * 10 + static_cast<uint64_t>(*at - '0');
        if (value > max) return nullptr;
    }
    if (*at != end) return nullptr;
    out = static_cast<uint32_t>(value);
    return end == 0 ? at : at + 1;
}

bool within_widest(const ports::ImageVersion& v) {
    return v.major <= kWidestVersion.major && v.minor <= kWidestVersion.minor &&
           v.revision <= kWidestVersion.revision && v.build <= kWidestVersion.build;
}

bool is_image(const SlotImage& slot, const ports::ImageVersion& version,
              const ports::ImageHash& hash, bool hashed) {
    if (hashed && slot.hashed) return slot.hash == hash;
    return slot.version == version;
}

ports::ImageVersion get_version(const uint8_t* in) {
    ports::ImageVersion v;
    v.major = in[0];
    v.minor = in[1];
    v.revision = get_u16(in + 2);
    v.build = get_u32(in + 4);
    return v;
}

}  // namespace

size_t to_blob(const UpdateRecord& record, uint8_t* out, size_t cap) {
    const size_t len = record.hashed ? kUpdateRecordBytes : kVersionsOnlyRecordBytes;
    if (cap < len) return 0;
    put_version(out, record.from);
    put_version(out + 8, record.to);
    if (!record.hashed) return len;
    std::memcpy(out + kVersionsOnlyRecordBytes, record.from_hash.bytes, ports::ImageHash::kBytes);
    std::memcpy(out + kVersionsOnlyRecordBytes + ports::ImageHash::kBytes, record.to_hash.bytes,
                ports::ImageHash::kBytes);
    return kUpdateRecordBytes;
}

bool from_blob(const uint8_t* blob, size_t len, UpdateRecord& out) {
    if (len != kVersionsOnlyRecordBytes && len != kUpdateRecordBytes) return false;
    out = UpdateRecord{};
    out.from = get_version(blob);
    out.to = get_version(blob + 8);
    if (len == kVersionsOnlyRecordBytes) return true;
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

int compare(const ports::ImageVersion& a, const ports::ImageVersion& b) {
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.revision != b.revision) return a.revision < b.revision ? -1 : 1;
    if (a.build != b.build) return a.build < b.build ? -1 : 1;
    return 0;
}

const char* version_refusal(const ports::ImageVersion& running, const ports::ImageVersion& incoming,
                            bool downgrade_allowed) {
    if (!downgrade_allowed && compare(incoming, running) < 0) return "older";
    return nullptr;
}

int format_version(const ports::ImageVersion& version, char* out, size_t cap) {
    if (cap < kVersionTextCap || !within_widest(version)) {
        if (cap > 0) out[0] = 0;
        return 0;
    }
    int n = fmt_uint(out, version.major);
    out[n++] = '.';
    n += fmt_uint(out + n, version.minor);
    out[n++] = '.';
    n += fmt_uint(out + n, version.revision);
    out[n++] = '+';
    n += fmt_uint(out + n, version.build);
    out[n] = 0;
    return n;
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

int format_hub_image(const HubImageReport& report, char* out, size_t cap) {
    if (cap < kHubImageTextCap) {
        if (cap > 0) out[0] = 0;
        return 0;
    }
    const char* word = nullptr;
    switch (report.holding) {
        case HubImage::None: word = "none"; break;
        case HubImage::Missing: word = "missing"; break;
        case HubImage::Corrupt: word = "corrupt"; break;
        case HubImage::Unreadable: word = "unreadable"; break;
        case HubImage::Writing: word = "writing"; break;
        case HubImage::Held: break;
    }
    if (word != nullptr) {
        const int n = fmt_string(out, word);
        out[n] = 0;
        return n;
    }
    static constexpr char kLowerHex[] = "0123456789abcdef";
    int n = 0;
    for (uint8_t byte : report.digest) {
        out[n++] = kLowerHex[byte >> 4];
        out[n++] = kLowerHex[byte & 0x0F];
    }
    out[n] = 0;
    return n;
}

}  // namespace skyblip::dfu
