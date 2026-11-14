#include "core/traffic/range.h"

#include "core/flight/extrapolate.h"
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/units/units.h"
#include "protocols/common/geometry.h"

namespace skyblip::traffic {

bool range_to(const model::Ownship& own, const model::AircraftObservation& observation,
              RangeRow& out) {
    int32_t north_m = 0, east_m = 0, up_m = 0;
    bool up_valid = false;
    if (!flight::relative_north_east_up(own, observation, north_m, east_m, up_m, up_valid))
        return false;

    out = RangeRow{};
    out.address = observation.address;
    out.address_mapping_table = observation.address_mapping_table;
    out.source = observation.source;
    out.up_m = up_m;
    out.up_valid = up_valid;
    out.ground_m = static_cast<int32_t>(wire::hypotenuse(north_m, east_m));
    return true;
}

namespace {

// INFO: fc 15nov26 a target with no altitude may be at ours, so it ranks inside the window
bool in_window(const RangeRow& row) {
    return !row.up_valid || (row.up_m < 0 ? -row.up_m : row.up_m) <= kAdvisoryAltitudeM;
}

}  // namespace

bool outranks(const RangeRow& row, const RangeRow& other) {
    if (row.alarm_level != other.alarm_level) return row.alarm_level > other.alarm_level;
    if (in_window(row) != in_window(other)) return in_window(row);
    return row.ground_m < other.ground_m;
}

int rank_by_relevance(const TrafficTable& table, const CallsignTable& callsigns,
                      const model::Ownship& own, RangeRow* out, int capacity) {
    int count = 0;
    for (int i = 0; i < TrafficTable::kMaxTargets; i++) {
        const Target* target = table.at(i);
        if (!target || !target->used) continue;
        RangeRow row;
        if (!range_to(own, target->observation, row)) continue;
        row.callsign = callsigns.find(row.address_mapping_table, row.address);
        row.alarm_level = target->alarm_level;
        row.slot = i;

        int position = count;
        while (position > 0 && outranks(row, out[position - 1])) position--;
        if (position >= capacity) continue;
        for (int j = (count < capacity ? count : capacity - 1); j > position; j--)
            out[j] = out[j - 1];
        out[position] = row;
        if (count < capacity) count++;
    }
    return count;
}

}  // namespace skyblip::traffic
