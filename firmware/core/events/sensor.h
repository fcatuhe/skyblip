#ifndef SKYBLIP_CORE_EVENTS_SENSOR_H
#define SKYBLIP_CORE_EVENTS_SENSOR_H

#include <cstdint>

namespace skyblip::events {
struct BaroSample {
    uint32_t pressure_mpa;
    uint32_t at_ms;
    int16_t temperature_decicelsius;
    bool temperature_valid;
};

struct AccelSample {
    int16_t right_mg;
    int16_t up_mg;
    int16_t aft_mg;
    uint32_t at_ms;
};

// What the divider read, and whether something is feeding the charger. Whether
// that reading is the cell at all is core/power's problem, not the board's.
struct BatterySample {
    uint16_t millivolts;
    bool external_power;
};

}  // namespace skyblip::events

#endif
