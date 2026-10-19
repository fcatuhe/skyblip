#include "core/traffic/range.h"

#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/protocol/nmea_out.h"
#include "core/util/intmath.h"

namespace skyblip::traffic {

bool range_to(const model::OwnState& own, const model::AircraftObs& obs, RangeRow& out) {
    int32_t north_m = 0, east_m = 0, up_m = 0;
    if (!protocol::relative_ned(own, obs, north_m, east_m, up_m)) return false;

    out = RangeRow{};
    out.address = obs.address;
    out.address_mapping_table = obs.address_mapping_table;
    out.source = obs.source;
    out.up_m = up_m;
    out.ground_m = static_cast<int32_t>(idistance(north_m, east_m));
    return true;
}

int rank_by_range(const TrafficTable& table, const CallsignTable& callsigns,
                  const model::OwnState& own, RangeRow* out, int cap) {
    int n = 0;
    for (int i = 0; i < TrafficTable::kCapacity; i++) {
        const Target* t = table.at(i);
        if (!t || !t->used) continue;
        RangeRow row;
        if (!range_to(own, t->obs, row)) continue;
        row.callsign = callsigns.find(row.address_mapping_table, row.address);

        int at = n;
        while (at > 0 && out[at - 1].ground_m > row.ground_m) at--;
        if (at >= cap) continue;
        for (int j = (n < cap ? n : cap - 1); j > at; j--) out[j] = out[j - 1];
        out[at] = row;
        if (n < cap) n++;
    }
    return n;
}

}  // namespace skyblip::traffic
