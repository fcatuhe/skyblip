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

// INFO: fc 18oct26 the 21 of 'activate 3F2A skyBlip' and room, inside a scan response's 29
constexpr int kBluetoothNameBytes = 24;
// INFO: fc 18oct26 what is left for the product word beside ' 00012A'
constexpr int kProductWordBytes = kBluetoothNameBytes - 7;
static_assert(sizeof("activate 3F2A skyBlip") - 1 <= kBluetoothNameBytes,
              "the unactivated name fits");

int bluetooth_name(char* out, int cap, const char* product, const Activation& activation);

}  // namespace skyblip::identity

#endif
