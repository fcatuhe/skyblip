#ifndef SKYBLIP_CORE_EVENTS_CENTRAL_H
#define SKYBLIP_CORE_EVENTS_CENTRAL_H

#include <array>
#include <cstdint>

namespace skyblip::events {

// INFO: fc 11nov26 its type and six bytes, least significant first, as Zephyr's bt_addr_le_t
struct BluetoothAddress {
    uint8_t type{0};
    std::array<uint8_t, 6> bytes{};
};

inline bool operator==(const BluetoothAddress& a, const BluetoothAddress& b) {
    return a.type == b.type && a.bytes == b.bytes;
}
inline bool operator!=(const BluetoothAddress& a, const BluetoothAddress& b) { return !(a == b); }

// INFO: fc 11nov26 the low 16 bits of the address a unit advertises with (core/identity/README.md)
constexpr uint16_t bluetooth_tag(const BluetoothAddress& address) {
    return static_cast<uint16_t>(address.bytes[0] | (address.bytes[1] << 8));
}

constexpr int kAdvertisedNameBytes = 29;

enum class CentralEventType : uint8_t { Found, Up, Down };

struct CentralEvent {
    CentralEventType type{CentralEventType::Down};
    BluetoothAddress address{};
    uint16_t payload_bytes{0};
    char name[kAdvertisedNameBytes + 1]{};
};

}  // namespace skyblip::events

#endif
