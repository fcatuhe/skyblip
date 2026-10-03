#ifndef SKYBLIP_CORE_DFU_UPDATE_H
#define SKYBLIP_CORE_DFU_UPDATE_H

#include <cstddef>
#include <cstdint>
#include <optional>

#include "ports/dfu.h"

namespace skyblip::dfu {

enum class ImageState : uint8_t { Confirmed = 0, Probation = 1, Reverted = 2, Refused = 3 };

const char* to_string(ImageState state);

struct UpdateRecord {
    ports::ImageVersion from{};
    ports::ImageVersion to{};
};

constexpr size_t kUpdateRecordBytes = 16;

size_t to_blob(const UpdateRecord& record, uint8_t* out, size_t cap);
bool from_blob(const uint8_t* blob, size_t len, UpdateRecord& out);

enum class Outcome : uint8_t { Landed, Reverted, Refused, Unrelated };

Outcome outcome(const UpdateRecord& record, const ports::ImageVersion& running,
                std::optional<ports::ImageVersion> staged);

int compare(const ports::ImageVersion& a, const ports::ImageVersion& b);

const char* version_refusal(const ports::ImageVersion& running, const ports::ImageVersion& incoming,
                            bool downgrade_allowed);

constexpr size_t kVersionTextCap = 25;

int format_version(const ports::ImageVersion& version, char* out, size_t cap);

bool parse_version(const char* text, ports::ImageVersion& out);

}  // namespace skyblip::dfu

#endif
