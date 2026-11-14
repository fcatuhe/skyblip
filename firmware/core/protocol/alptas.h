#ifndef SKYBLIP_CORE_PROTOCOL_ALPTAS_H
#define SKYBLIP_CORE_PROTOCOL_ALPTAS_H

#include <cstdint>

#include "core/model/aircraft.h"
#include "core/util/result.h"

namespace skyblip::protocol {

uint8_t alptas_type_to_adsl_category(
    uint8_t alptas_type);  // inverse of adsl_category_to_alptas_type
uint8_t alptas_flight_state(uint8_t alptas_airborne);
uint8_t alptas_address_type_to_address_mapping_table(uint8_t alptas_address_type);
uint8_t address_mapping_table_to_alptas_address_type(uint8_t address_mapping_table);

// INFO: fc 13nov26 out is left as it was unless the frame decodes, so a caller may try a second key
Status alptas_decode(const uint8_t* frame, uint32_t rx_utc_s, int32_t reference_latitude_1e7,
                     int32_t reference_longitude_1e7, model::AircraftObservation& out);

Status alptas_encode(uint8_t* frame, const model::AircraftObservation& observation, uint32_t utc_s);

}

#endif
