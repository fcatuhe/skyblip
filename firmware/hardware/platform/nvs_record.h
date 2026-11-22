#ifndef SKYBLIP_HARDWARE_PLATFORM_NVS_RECORD_H
#define SKYBLIP_HARDWARE_PLATFORM_NVS_RECORD_H

#include <cstdint>

namespace skyblip::platform {

// INFO: fc 21nov26 NVS writes its own sector-close and gc-done entries under 0xFFFF, nvs.c
constexpr uint16_t kNvsReservedRecordId = 0xFFFF;

constexpr uint16_t nvs_record_id(const char* key) {
    uint32_t hash = 2166136261u;
    for (const char* character = key; *character; ++character)
        hash = (hash ^ static_cast<uint8_t>(*character)) * 16777619u;
    // INFO: fc 20nov26 the low bit is forced so that no key hashes to id 0
    return static_cast<uint16_t>((hash & 0xFFFF) | 1u);
}

}  // namespace skyblip::platform

#endif
