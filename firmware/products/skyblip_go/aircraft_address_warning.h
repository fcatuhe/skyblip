#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_AIRCRAFT_ADDRESS_WARNING_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_AIRCRAFT_ADDRESS_WARNING_H

#include <cstdint>

#include "products/skyblip_go/settings.h"

namespace skyblip::go {

// INFO: fc 11nov26 #178 calls paired() on each Bravo health frame and unpaired() on losing it
class AircraftAddressWarning {
   public:
    void paired(bool bravo_aircraft_address_set) {
        paired_ = true;
        bravo_aircraft_address_set_ = bravo_aircraft_address_set;
    }

    void unpaired() {
        paired_ = false;
        bravo_aircraft_address_set_ = false;
    }

    bool stands(uint32_t aircraft_address) const {
        return paired_ && !bravo_aircraft_address_set_ && aircraft_address == kAircraftAddressUnset;
    }

   private:
    bool paired_{false};
    bool bravo_aircraft_address_set_{false};
};

}  // namespace skyblip::go

#endif
