#ifndef SKYBLIP_CORE_DFU_UPDATE_H
#define SKYBLIP_CORE_DFU_UPDATE_H

#include <cstddef>
#include <cstdint>

#include "ports/dfu.h"

namespace skyblip::dfu {

enum class ImageState : uint8_t { Confirmed = 0, Probation = 1, Reverted = 2 };

const char* to_string(ImageState state);

struct UpdateRecord {
    ports::ImageVersion from{};
    ports::ImageVersion to{};
    ports::ImageHash from_hash{};
    ports::ImageHash to_hash{};
    bool hashed{false};
};

constexpr size_t kVersionsOnlyRecordBytes = 16;
constexpr size_t kUpdateRecordBytes = kVersionsOnlyRecordBytes + 2 * ports::ImageHash::kBytes;

size_t to_blob(const UpdateRecord& record, uint8_t* out, size_t cap);
bool from_blob(const uint8_t* blob, size_t len, UpdateRecord& out);

enum class Outcome : uint8_t { Landed, Reverted, Unrelated };

struct RunningImage {
    ports::ImageVersion version{};
    ports::ImageHash hash{};
    bool hashed{false};
};

Outcome outcome(const UpdateRecord& record, const RunningImage& running);

enum class HubImage : uint8_t { None, Missing, Corrupt, Unreadable, Writing, Held };

struct HubImageReport {
    static constexpr size_t kDigestBytes = 8;
    HubImage holding{HubImage::None};
    uint8_t digest[kDigestBytes]{};
};

constexpr size_t kHubImageTextCap = 2 * HubImageReport::kDigestBytes + 1;

int format_hub_image(const HubImageReport& report, char* out, size_t cap);

constexpr size_t kVersionTextCap = 25;

int format_version(const ports::ImageVersion& version, char* out, size_t cap);

bool parse_version(const char* text, ports::ImageVersion& out);

}  // namespace skyblip::dfu

#endif
