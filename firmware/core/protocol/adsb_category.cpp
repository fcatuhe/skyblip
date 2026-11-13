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

}  // namespace skyblip::protocol
