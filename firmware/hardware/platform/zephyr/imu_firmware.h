#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_IMU_FIRMWARE_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_IMU_FIRMWARE_H

#include <cstddef>
#include <cstdint>

#include "core/util/sha256.h"

namespace skyblip::platform::zephyr {

extern const uint8_t* const kImuFirmware;
extern const size_t kImuFirmwareBytes;
extern const Sha256::Digest kImuFirmwareDigest;

}  // namespace skyblip::platform::zephyr

#endif
