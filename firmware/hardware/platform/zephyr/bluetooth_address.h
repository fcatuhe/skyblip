#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_BLUETOOTH_ADDRESS_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_BLUETOOTH_ADDRESS_H
#if defined(__ZEPHYR__)

#include <zephyr/bluetooth/addr.h>

#include <cstddef>

#include "core/events/central.h"

namespace skyblip::platform::zephyr {

inline events::BluetoothAddress address_of(const bt_addr_le_t* address) {
    events::BluetoothAddress out{};
    out.type = address->type;
    for (size_t i = 0; i < out.bytes.size(); i++) out.bytes[i] = address->a.val[i];
    return out;
}

inline bt_addr_le_t le_address(const events::BluetoothAddress& address) {
    bt_addr_le_t out = {};
    out.type = address.type;
    for (size_t i = 0; i < address.bytes.size(); i++) out.a.val[i] = address.bytes[i];
    return out;
}

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
