#ifndef SKYBLIP_PORTS_INDICATOR_H
#define SKYBLIP_PORTS_INDICATOR_H

#include <cstdint>

#include "core/indication/colour.h"

namespace skyblip::ports {

class Indicator {
   public:
    virtual ~Indicator() = default;

    // INFO: fc 20nov26 call it only when the colour changes, not on every pass of the loop
    virtual void show(indication::Colour colour) { (void)colour; }

    // INFO: fc 20nov26 not show(None): it releases the pins, or an active-low LED feeds a dead rail
    virtual void park() {}
};

}  // namespace skyblip::ports

#endif
