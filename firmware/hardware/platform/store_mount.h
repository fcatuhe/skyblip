#ifndef SKYBLIP_HARDWARE_PLATFORM_STORE_MOUNT_H
#define SKYBLIP_HARDWARE_PLATFORM_STORE_MOUNT_H

#include <cstdint>

namespace skyblip::platform {

// INFO: fc 12nov26 Meshtastic v2.8.0's WarmNodeStore pages open on these, WarmNodeStore.cpp:16-18
constexpr uint32_t kWarmRingMagicV1 = 0x474E5257u;
constexpr uint32_t kWarmRingMagicV2 = 0x324E5257u;
constexpr uint32_t kWarmRingMagicV3 = 0x334E5257u;

enum class MountAttempt : uint8_t { Mounted, RefusedContent, Failed };

enum class StoreMount : uint8_t { Mounted, ClearedForeignRing, ClearedRefused, Down };

constexpr bool is_warm_node_store_page(uint32_t first_word) {
    return first_word == kWarmRingMagicV1 || first_word == kWarmRingMagicV2 ||
           first_word == kWarmRingMagicV3;
}

template <class Flash>
bool holds_warm_node_store(Flash& flash, int sectors) {
    for (int sector = 0; sector < sectors; sector++) {
        uint32_t first_word = 0;
        if (flash.read_word(sector, first_word) && is_warm_node_store_page(first_word)) return true;
    }
    return false;
}

// INFO: fc 12nov26 erasing what another firmware left is the intent, and the caller logs it
template <class Flash>
StoreMount mount_store(Flash& flash, int sectors) {
    if (holds_warm_node_store(flash, sectors)) {
        if (!flash.erase()) return StoreMount::Down;
        if (flash.mount() != MountAttempt::Mounted) return StoreMount::Down;
        return StoreMount::ClearedForeignRing;
    }
    const MountAttempt first = flash.mount();
    if (first == MountAttempt::Mounted) return StoreMount::Mounted;
    if (first == MountAttempt::Failed) return StoreMount::Down;
    if (!flash.erase()) return StoreMount::Down;
    if (flash.mount() != MountAttempt::Mounted) return StoreMount::Down;
    return StoreMount::ClearedRefused;
}

}  // namespace skyblip::platform

#endif
