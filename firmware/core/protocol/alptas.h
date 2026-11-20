#ifndef SKYBLIP_CORE_PROTOCOL_ALPTAS_H
#define SKYBLIP_CORE_PROTOCOL_ALPTAS_H

#include <array>
#include <cstdint>

#include "core/events/stamp.h"
#include "core/model/aircraft.h"
#include "core/util/result.h"

namespace skyblip::protocol {

uint8_t alptas_type_to_adsl_category(uint8_t alptas_type);
uint8_t alptas_flight_state(uint8_t alptas_airborne);
uint8_t alptas_urgency_to_emergency(uint8_t alptas_urgency);

// INFO: fc 17nov26 Frame 2 ends 1.2 s after the fix, and a key accepted a second off adds one
constexpr int32_t kAlptasFixAgeCeilingMs = 2500;
int32_t alptas_fix_age_ms(uint8_t fix_quarters, const events::Stamp& heard);
uint8_t alptas_address_type_to_address_mapping_table(uint8_t alptas_address_type);
uint8_t address_mapping_table_to_alptas_address_type(uint8_t address_mapping_table);

// INFO: fc 13nov26 out is left as it was unless the frame decodes, so a caller may try a second key
Status alptas_decode(const uint8_t* frame, uint32_t rx_utc_s, int32_t reference_latitude_1e7,
                     int32_t reference_longitude_1e7, model::AircraftObservation& out);

// INFO: fc 17nov26 a random address moves every 2 to 10 s, so the receiver keeps the first it heard
class AlptasReceiver {
   public:
    static constexpr int kFollowed = 16;
    static constexpr uint32_t kFollowS = 20;

    Status decode(const uint8_t* frame, uint32_t rx_utc_s, int32_t reference_latitude_1e7,
                  int32_t reference_longitude_1e7, const events::Stamp& heard,
                  model::AircraftObservation& out);

   private:
    struct Followed {
        uint32_t current{0};
        uint32_t next{0};
        uint32_t shown{0};
        uint32_t heard_s{0};
        bool used{false};
    };

    std::array<Followed, kFollowed> followed_{};

    uint32_t shown_address(uint32_t address, uint8_t random, uint32_t heard_s);
};

Status alptas_encode(uint8_t* frame, const model::AircraftObservation& observation, uint32_t utc_s);

}

#endif
