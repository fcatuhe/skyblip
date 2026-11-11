#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_SKYBLIP_UUID_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_SKYBLIP_UUID_H
#if defined(__ZEPHYR__)

#include <zephyr/bluetooth/uuid.h>

#include <cstdint>

// INFO: fc 18sep26 Our own base: config and log are our protocol, not a UART apps would misread.
#define SKYBLIP_UUID(id) \
    BT_UUID_128_ENCODE(0x69c21300 | (id), 0x0187, 0x4204, 0xa91d, 0x02eb8858b440)

namespace skyblip::platform::zephyr {

constexpr uint32_t kSkyblipServiceId = 0x01;
constexpr uint32_t kConfigCharacteristicId = 0x02;
constexpr uint32_t kLogCharacteristicId = 0x03;
constexpr uint32_t kPeerCharacteristicId = 0x04;

}  // namespace skyblip::platform::zephyr

#endif  // __ZEPHYR__
#endif
