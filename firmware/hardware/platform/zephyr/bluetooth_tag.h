#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_BLUETOOTH_TAG_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_BLUETOOTH_TAG_H
#if defined(__ZEPHYR__)

#include <hal/nrf_ficr.h>

#include <cstdint>

namespace skyblip::platform::zephyr {

inline uint16_t bluetooth_tag() {
    return static_cast<uint16_t>(nrf_ficr_deviceaddr_get(NRF_FICR, 0) & 0xFFFFu);
}

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
