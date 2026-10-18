#ifndef SKYBLIP_CORE_IDENTITY_ACTIVATION_H
#define SKYBLIP_CORE_IDENTITY_ACTIVATION_H

#include <cstdint>
#include <optional>

#include "core/settings/address.h"

namespace skyblip::identity {

struct Activation {
    std::optional<uint16_t> serial{};
    uint16_t bluetooth_tag{0};

    bool activated() const { return serial.has_value(); }
    settings::AirAddress address() const {
        if (!serial) return std::nullopt;
        return settings::address_of(*serial);
    }
};

inline bool operator==(const Activation& a, const Activation& b) {
    return a.serial == b.serial && a.bluetooth_tag == b.bluetooth_tag;
}
inline bool operator!=(const Activation& a, const Activation& b) { return !(a == b); }

// INFO: fc 18oct26 21 characters, inside the 29 the scan response leaves for a name
constexpr int kBluetoothNameCap = 24;

int bluetooth_name(char* out, int cap, const char* product, const Activation& activation);

}  // namespace skyblip::identity

#endif
