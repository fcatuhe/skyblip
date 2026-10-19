#include "core/traffic/range.h"

#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/units/units.h"
#include "core/util/intmath.h"

namespace skyblip::traffic {

bool relative_ned(const model::Ownship& own, const model::AircraftObservation& target,
                  int32_t& north_m, int32_t& east_m, int32_t& up_m) {
    if (!own.fix_valid || !target.position_valid) return false;
    int64_t dlat = static_cast<int64_t>(target.lat_1e7) - own.lat_1e7;
    int64_t dlon = wrapped_lon_1e7(static_cast<int64_t>(target.lon_1e7) - own.lon_1e7);
    north_m = static_cast<int32_t>(div_round<int64_t>(dlat * 11132, 1000000));
    int16_t ang = static_cast<int16_t>((static_cast<int64_t>(own.lat_1e7) * 65536) / 3600000000LL);
    int64_t coslat = icos(ang);
    int64_t east_um = div_round<int64_t>(dlon * 11132 * coslat, kTrigOne);
    east_m = static_cast<int32_t>(div_round<int64_t>(east_um, 1000000));
    up_m = target.alt_valid ? target.alt_m - to_metres(Millimetres(own.alt_hae_mm)).v : 0;
    return true;
}

bool range_to(const model::Ownship& own, const model::AircraftObservation& observation,
              RangeRow& out) {
    int32_t north_m = 0, east_m = 0, up_m = 0;
    if (!relative_ned(own, observation, north_m, east_m, up_m)) return false;

    out = RangeRow{};
    out.address = observation.address;
    out.address_mapping_table = observation.address_mapping_table;
    out.source = observation.source;
    out.up_m = up_m;
    out.ground_m = static_cast<int32_t>(idistance(north_m, east_m));
    return true;
}

int rank_by_range(const TrafficTable& table, const CallsignTable& callsigns,
                  const model::Ownship& own, RangeRow* out, int capacity) {
    int n = 0;
    for (int i = 0; i < TrafficTable::kCapacity; i++) {
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
