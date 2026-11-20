#ifndef SKYBLIP_PORTS_INVENTORY_H
#define SKYBLIP_PORTS_INVENTORY_H

#include <cstdint>

namespace skyblip::ports {

enum class HapticKind : uint8_t { None, PinMotor, WaveformDriver };

struct Inventory {
    // INFO: fc 20nov26 every address that answered, driven or not: 0x51 alone shows an RTC
    static constexpr int kMaxI2cAddresses = 8;
    uint8_t i2c_addresses[kMaxI2cAddresses]{};
    uint8_t i2c_count{0};

    uint8_t barometer_address{0};
    // INFO: fc 20nov26 never null, because the self-test page prints it
    const char* panel_name{"?"};
    HapticKind haptic{HapticKind::None};
    // INFO: fc 11nov26 an undriven RTC: T-Echo's PCF8563 at 0x51, or X1's YSN8900 on its own bus
    bool rtc_fitted{false};

    bool has_i2c_address(uint8_t address) const {
        for (uint8_t i = 0; i < i2c_count; i++)
            if (i2c_addresses[i] == address) return true;
        return false;
    }

    bool add_i2c_address(uint8_t address) {
        if (i2c_count >= kMaxI2cAddresses || has_i2c_address(address)) return false;
        uint8_t at = i2c_count;
        while (at > 0 && i2c_addresses[at - 1] > address) {
            i2c_addresses[at] = i2c_addresses[at - 1];
            at--;
        }
        i2c_addresses[at] = address;
        i2c_count++;
        return true;
    }
};

}  // namespace skyblip::ports

#endif
