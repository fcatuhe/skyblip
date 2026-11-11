#ifndef SKYBLIP_HARDWARE_MODEL_YSN8900_H
#define SKYBLIP_HARDWARE_MODEL_YSN8900_H

#include <cstdint>

#include "hardware/io/io.h"

namespace skyblip::models {

class Ysn8900 : public io::I2c {
   public:
    static constexpr uint8_t kAddress = 0x32;

    bool write(uint8_t address, const uint8_t*, size_t bytes) override {
        if (address != kAddress || !answers) return false;
        writes++;
        if (bytes == 0) probes++;
        return true;
    }

    bool read(uint8_t address, uint8_t* out, size_t bytes) override {
        if (address != kAddress || !answers) return false;
        reads++;
        for (size_t i = 0; i < bytes; i++) out[i] = 0;
        return true;
    }

    bool answers{true};
    int writes{0};
    int probes{0};
    int reads{0};
};

}  // namespace skyblip::models

#endif
