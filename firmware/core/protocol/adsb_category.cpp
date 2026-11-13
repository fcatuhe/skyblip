#include "core/protocol/adsb_category.h"

namespace skyblip::protocol {

uint8_t adsb_emitter_to_adsl_category(uint8_t type_code, uint8_t emitter_category) {
    static const uint8_t kSetA[8] = {0, 1, 2, 2, 2, 2, 2, 15};
    static const uint8_t kSetB[8] = {0, 4, 5, 8, 6, 0, 12, 0};
    if (emitter_category >= 8) return 0;
    switch (type_code) {
        case 3: return kSetB[emitter_category];
        case 4: return kSetA[emitter_category];
        default: return 0;
    }
}

uint8_t adsb_emitter_to_own_category(uint8_t type_code, uint8_t emitter_category) {
    const uint8_t traffic = adsb_emitter_to_adsl_category(type_code, emitter_category);
    switch (traffic) {
        case 2: return type_code == 4 && emitter_category == 6 ? 1 : traffic;
        case 15: return 3;
        case 12: return 11;
        default: return traffic;
    }
}

}  // namespace skyblip::protocol
