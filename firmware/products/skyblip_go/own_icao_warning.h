#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_OWN_ICAO_WARNING_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_OWN_ICAO_WARNING_H

#include <cstdint>

#include "products/skyblip_go/settings.h"

namespace skyblip::go {

// INFO: fc 11nov26 #178 calls paired() on each Bravo health frame and unpaired() on losing it
class OwnIcaoWarning {
   public:
    void paired(bool bravo_own_icao_set) {
        paired_ = true;
        bravo_own_icao_set_ = bravo_own_icao_set;
    }

    void unpaired() {
        paired_ = false;
        bravo_own_icao_set_ = false;
    }

    bool stands(uint32_t own_icao) const {
        return paired_ && !bravo_own_icao_set_ && own_icao == kOwnIcaoUnset;
    }

    bool take_notice(uint32_t own_icao) {
        if (noticed_ || !stands(own_icao)) return false;
        noticed_ = true;
        return true;
    }

   private:
    bool paired_{false};
    bool bravo_own_icao_set_{false};
    bool noticed_{false};
};

}  // namespace skyblip::go

#endif
