#ifndef SKYBLIP_PORTS_CAPABILITIES_H
#define SKYBLIP_PORTS_CAPABILITIES_H

#include <cstdint>

namespace skyblip::ports {

// INFO: fc 20nov26 append bits: products/skyblip_go/product.h asserts each self-test row's shift
enum class Capability : uint32_t {
    None = 0,
    Rf = 1u << 0,
    Gnss = 1u << 1,
    Display = 1u << 2,
    Barometer = 1u << 3,
    Buzzer = 1u << 4,
    Haptic = 1u << 5,
    Link = 1u << 6,
    Storage = 1u << 7,
    Dfu = 1u << 8,
    Contacts = 1u << 9,
    Battery = 1u << 10,
    DieTemperature = 1u << 11,
    HapticDriver = 1u << 12,
    Indicator = 1u << 13,
    Inclinometer = 1u << 14,
    AdsbReceiver = 1u << 15,
};

using Capabilities = Capability;

constexpr Capabilities operator|(Capabilities left, Capabilities right) {
    return static_cast<Capabilities>(static_cast<uint32_t>(left) | static_cast<uint32_t>(right));
}

constexpr Capabilities& operator|=(Capabilities& left, Capabilities right) {
    left = left | right;
    return left;
}

constexpr bool has(Capabilities set, Capabilities wanted) {
    return (static_cast<uint32_t>(set) & static_cast<uint32_t>(wanted)) ==
           static_cast<uint32_t>(wanted);
}

constexpr Capabilities without(Capabilities set, Capabilities removed) {
    return static_cast<Capabilities>(static_cast<uint32_t>(set) & ~static_cast<uint32_t>(removed));
}

}  // namespace skyblip::ports

#endif
