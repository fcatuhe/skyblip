#ifndef SKYBLIP_BOARDS_T_ECHO_PLUS_I2C_SCAN_H
#define SKYBLIP_BOARDS_T_ECHO_PLUS_I2C_SCAN_H

#include <cstdint>

#include "hardware/io/i2c_scan.h"
#include "hardware/io/io.h"
#include "hardware/parts/bhi260/bhi260.h"
#include "hardware/parts/bme280/bme280.h"
#include "hardware/parts/drv2605/drv2605.h"
#include "ports/inventory.h"

namespace skyblip::boards::t_echo_plus {

// INFO: fc 20nov26 the PCF8563 RTC, fitted and deliberately undriven
constexpr uint8_t kRtcAddress = 0x51;

constexpr const char* i2c_role(uint8_t address) {
    switch (address) {
        case parts::Bhi260::kAddress:
        case parts::Bhi260::kAddressAlternate: return "IMU";
        case kRtcAddress: return "RTC";
        case parts::Drv2605::kAddress: return "HAPTIC";
        case parts::Bme280::kAddressPrimary:
        case parts::Bme280::kAddressAlternate: return "BARO";
    }
    return nullptr;
}

inline void scan_i2c(io::I2c& bus, ports::Inventory& inventory) {
    io::scan_i2c(bus, inventory);
    inventory.rtc_fitted = inventory.has_i2c_address(kRtcAddress);
}

}  // namespace skyblip::boards::t_echo_plus

#endif
