#ifndef SKYBLIP_CORE_PROTOCOL_ADSB_CATEGORY_H
#define SKYBLIP_CORE_PROTOCOL_ADSB_CATEGORY_H

#include <cstdint>

namespace skyblip::protocol {

uint8_t adsb_emitter_to_adsl_category(uint8_t type_code, uint8_t emitter_category);

}  // namespace skyblip::protocol

#endif
