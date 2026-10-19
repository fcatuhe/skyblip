#include "core/traffic/range.h"

#include "core/flight/extrapolate.h"
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/units/units.h"
#include "core/util/intmath.h"

namespace skyblip::traffic {

bool range_to(const model::Ownship& own, const model::AircraftObservation& observation,
              RangeRow& out) {
    int32_t north_m = 0, east_m = 0, up_m = 0;
    if (!flight::relative_north_east_up(own, observation, north_m, east_m, up_m)) return false;

    out = RangeRow{};
    out.address = observation.address;
    out.address_mapping_table = observation.address_mapping_table;
    out.source = observation.source;
    out.up_m = up_m;
    out.ground_m = static_cast<int32_t>(hypotenuse(north_m, east_m));
    return true;
}

int rank_by_range(const TrafficTable& table, const CallsignTable& callsigns,
                  const model::Ownship& own, RangeRow* out, int capacity) {
    int count = 0;
    for (int i = 0; i < TrafficTable::kMaxTargets; i++) {
        const Target* target = table.at(i);
        if (!target || !target->used) continue;
        RangeRow row;
        if (!range_to(own, target->observation, row)) continue;
        row.callsign = callsigns.find(row.address_mapping_table, row.address);

        int position = count;
        while (position > 0 && out[position - 1].ground_m > row.ground_m) position--;
        if (position >= capacity) continue;
        for (int j = (count < capacity ? count : capacity - 1); j > position; j--)
            out[j] = out[j - 1];
        out[position] = row;
        if (count < capacity) count++;
    }
    return count;
}

}  // namespace skyblip::traffic
