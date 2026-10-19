#ifndef SKYBLIP_PORTS_CAPABILITIES_H
#define SKYBLIP_PORTS_CAPABILITIES_H

#include <cstdint>

namespace skyblip::ports {

enum class Capability : uint32_t {
    None = 0,
    Rf = 1u << 0,
    Gnss = 1u << 1,
    Display = 1u << 2,
    Baro = 1u << 3,
    Buzzer = 1u << 4,
    Haptic = 1u << 5,
    Link = 1u << 6,
    Storage = 1u << 7,
    Dfu = 1u << 8,
    Contacts = 1u << 9,
    Battery = 1u << 10,
    // The SoC's own die sensor (ports/die_temperature.h). Next free shift, and the
    // existing eleven are not renumbered: nothing persists this bitset, but
    // products/skyblip_go/product.h asserts it row by row on the self-test page,
    // and a shift that moved would move a row with it.
    DieTemperature = 1u << 11,
    HapticDriver = 1u << 12,
    // A status lamp (ports/indicator.h). E-paper holds its last image with the
    // rails down, so on a board without this the device has no way at all to say
    // it is alive. It is granted from the devicetree, not from the board being a
    // Plus: absent, the same table in core/indication runs and lights nothing.
    Indicator = 1u << 13,
    Inclinometer = 1u << 14,
};

using Capabilities = Capability;

constexpr Capabilities operator|(Capabilities a, Capabilities b) {
    return static_cast<Capabilities>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr Capabilities& operator|=(Capabilities& a, Capabilities b) {
    a = a | b;
    return a;
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
