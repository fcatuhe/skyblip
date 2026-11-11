#ifndef SKYBLIP_HARDWARE_IO_I2C_SCAN_H
#define SKYBLIP_HARDWARE_IO_I2C_SCAN_H

#include <cstdint>

#include "hardware/io/io.h"
#include "ports/inventory.h"

namespace skyblip::io {

// INFO: fc 12nov26 I2C reserves 0x00-0x07 and 0x78-0x7F, so no part can answer there
constexpr uint8_t kI2cFirstAddress = 0x08;
constexpr uint8_t kI2cLastAddress = 0x77;

// INFO: fc 12nov26 a zero-length write addresses a part and stops, as Zephyr's i2c shell scan does
inline void scan_i2c(I2c& bus, ports::Inventory& inventory) {
    for (uint8_t address = kI2cFirstAddress; address <= kI2cLastAddress; address++)
        if (bus.write(address, nullptr, 0)) inventory.add_i2c_address(address);
}

}  // namespace skyblip::io

#endif
