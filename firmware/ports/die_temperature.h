#ifndef SKYBLIP_PORTS_DIE_TEMPERATURE_H
#define SKYBLIP_PORTS_DIE_TEMPERATURE_H

#include <cstdint>

namespace skyblip::ports {

class DieTemperature {
   public:
    virtual ~DieTemperature() = default;

    // INFO: fc 20nov26 false leaves die_dc untouched, so a reading nobody took is never published
    virtual bool read(int16_t& die_dc) {
        (void)die_dc;
        return false;
    }
};

}  // namespace skyblip::ports

#endif
