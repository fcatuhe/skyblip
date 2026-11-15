#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_DEVICE_ID_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_DEVICE_ID_H
#if defined(__ZEPHYR__)

#include <hal/nrf_ficr.h>

#include <cstdint>

#include "core/identity/messages.h"

namespace skyblip::platform::zephyr {

// INFO: fc 15nov26 the bytes Zephyr's hwinfo hands the Go, read from FICR so a keyless unit needs
// no driver
inline identity::DeviceId device_id() {
    identity::DeviceId id{};
    const uint32_t words[] = {nrf_ficr_deviceid_get(NRF_FICR, 1),
                              nrf_ficr_deviceid_get(NRF_FICR, 0)};
    for (int word = 0; word < 2; word++)
        for (int byte = 0; byte < 4; byte++)
            id[word * 4 + byte] = static_cast<uint8_t>(words[word] >> (24 - 8 * byte));
    return id;
}

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
