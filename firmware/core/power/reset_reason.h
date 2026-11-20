#ifndef SKYBLIP_CORE_POWER_RESET_REASON_H
#define SKYBLIP_CORE_POWER_RESET_REASON_H

#include <cstdint>

namespace skyblip::power {

enum class ResetCause : uint32_t {
    None = 0,
    PowerOn = 1u << 0,
    Pin = 1u << 1,
    Brownout = 1u << 2,
    Software = 1u << 3,
    Watchdog = 1u << 4,
    Lockup = 1u << 5,
    LowPowerWake = 1u << 6,
    Debug = 1u << 7,
    // INFO: fc 05aug26 Zephyr's hwinfo maps VBUS onto RESET_POR, so the adapter reads the bit
    // itself and wake.h can refuse a charger wake
    UsbVbus = 1u << 8,
};

constexpr ResetCause operator|(ResetCause left, ResetCause right) {
    return static_cast<ResetCause>(static_cast<uint32_t>(left) | static_cast<uint32_t>(right));
}

constexpr ResetCause& operator|=(ResetCause& left, ResetCause right) {
    left = left | right;
    return left;
}

constexpr bool has_cause(ResetCause set, ResetCause wanted) {
    return (static_cast<uint32_t>(set) & static_cast<uint32_t>(wanted)) != 0;
}

enum class ResetReason : uint8_t {
    Unknown = 0,
    PowerOn = 1,
    Pin = 2,
    Brownout = 3,
    Software = 4,
    Watchdog = 5,
    Lockup = 6,
    ChargerWake = 7,
    LowPowerWake = 8,
    Debug = 9,
};

// INFO: hk 02aug26 RESETREAS latches until written back (nRF52840 PS v1.8 §5.3.3), so a fault cause
// outranks a benign one set beside it
ResetReason classify(ResetCause causes);

const char* to_string(ResetReason reason);

}  // namespace skyblip::power

#endif
