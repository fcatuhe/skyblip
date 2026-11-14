#ifndef SKYBLIP_CORE_TRAFFIC_RANGE_H
#define SKYBLIP_CORE_TRAFFIC_RANGE_H

#include <cstdint>

#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/traffic/alarm.h"
#include "core/traffic/callsigns.h"
#include "core/traffic/table.h"

namespace skyblip::traffic {

struct RangeRow {
    uint32_t address{0};
    uint8_t address_mapping_table{0};
    model::Source source{model::Source::AdslDirect};
    int32_t ground_m{0};
    int32_t up_m{0};
    bool up_valid{true};
    Level alarm_level{Level::None};
    int slot{-1};
    // INFO: fc 20sep26 into the store the rows were ranked against, nullptr while nothing named it
    const char* callsign{nullptr};
};

// INFO: fc 19sep26 ground distance, what the radar ring and the alarm mean by distance
bool range_to(const model::Ownship& own, const model::AircraftObservation& observation,
              RangeRow& out);

// INFO: fc 15nov26 an alarm first, then the advisory's height window, then the nearest
bool outranks(const RangeRow& row, const RangeRow& other);

// Most relevant first, at most capacity rows. Returns how many were filled.
int rank_by_relevance(const TrafficTable& table, const CallsignTable& callsigns,
                      const model::Ownship& own, RangeRow* out, int capacity);

}  // namespace skyblip::traffic

#endif
