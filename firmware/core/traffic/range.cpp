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
    int n = 0;
    for (int i = 0; i < TrafficTable::kMaxTargets; i++) {
        const Target* t = table.at(i);
        if (!t || !t->used) continue;
        RangeRow row;
        if (!range_to(own, t->observation, row)) continue;
        row.callsign = callsigns.find(row.address_mapping_table, row.address);

        int at = n;
        while (at > 0 && out[at - 1].ground_m > row.ground_m) at--;
        if (at >= capacity) continue;
        for (int j = (n < capacity ? n : capacity - 1); j > at; j--) out[j] = out[j - 1];
        out[at] = row;
        if (n < capacity) n++;
    }
    return n;
}

}  // namespace skyblip::traffic
