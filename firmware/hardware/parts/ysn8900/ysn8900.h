#ifndef SKYBLIP_HARDWARE_PARTS_YSN8900_YSN8900_H
#define SKYBLIP_HARDWARE_PARTS_YSN8900_YSN8900_H

#include <cstdint>

#include "hardware/io/io.h"

namespace skyblip::parts {

class Ysn8900 {
   public:
    // TODO: fc 11nov26 SoftRF d091924 nRF52.h:154 address, unverified until #169 benches a unit
    static constexpr uint8_t kAddress = 0x32;

    explicit Ysn8900(io::I2c& bus) : bus_(bus) {}

    bool answers() const { return bus_.write(kAddress, nullptr, 0); }

   private:
    io::I2c& bus_;
};

}  // namespace skyblip::parts

#endif
