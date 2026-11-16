#ifndef SKYBLIP_PRODUCTS_COMMON_ALARM_H
#define SKYBLIP_PRODUCTS_COMMON_ALARM_H

#include <cstdint>

#include "ports/capabilities.h"

namespace skyblip::common {

// INFO: fc 15nov26 each step adds to the one below: visual interrupts, soft and loud also sound
enum class Alarm : uint8_t { Off = 0, Visual = 1, Soft = 2, Loud = 3 };

constexpr bool interrupts(Alarm alarm) { return alarm != Alarm::Off; }
constexpr bool audible(Alarm alarm) { return alarm == Alarm::Soft || alarm == Alarm::Loud; }

constexpr bool can_sound(ports::Capabilities fitted) {
    return ports::has(fitted, ports::Capability::Buzzer) ||
           ports::has(fitted, ports::Capability::Haptic);
}

}  // namespace skyblip::common

#endif
