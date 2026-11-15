#include "core/dfu/bootloader_keys.h"

#include <algorithm>
#include <cstring>

#include "core/util/sha256.h"

namespace skyblip::dfu {

namespace {

constexpr uint8_t kP256KeyPrefix[] = {0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48,
                                      0xce, 0x3d, 0x02, 0x01, 0x06, 0x08, 0x2a, 0x86, 0x48,
                                      0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00};
constexpr size_t kP256KeyBytes = 91;
constexpr size_t kStepBytes = 256;

void add(ports::BootloaderKeys& keys, const ports::SigningKeyHash& key) {
    if (keys.trusts(key) || keys.count == ports::BootloaderKeys::kCapacity) return;
    keys.keys[keys.count++] = key;
}

}  // namespace

bool find_bootloader_keys(ImageReader& bootloader, ports::BootloaderKeys& out) {
    out = {};
    uint8_t window[kStepBytes + kP256KeyBytes - 1];
    for (uint32_t at = 0; at < bootloader.bytes(); at += kStepBytes) {
        const size_t bytes = std::min<size_t>(sizeof(window), bootloader.bytes() - at);
        if (!bootloader.read(at, window, bytes)) return false;
        for (size_t i = 0; i < kStepBytes && i + kP256KeyBytes <= bytes; i++)
            if (std::memcmp(window + i, kP256KeyPrefix, sizeof(kP256KeyPrefix)) == 0)
                add(out, Sha256::of(window + i, kP256KeyBytes));
    }
    return out.count > 0;
}

}  // namespace skyblip::dfu
