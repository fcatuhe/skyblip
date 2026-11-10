#include "core/traffic/gate.h"

#include <cstdlib>

#include "core/flight/extrapolate.h"
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/util/intmath.h"

namespace skyblip::traffic {

bool outside_adsb_gate(const model::Ownship& own, const model::AircraftObservation& observation) {
    int32_t north_m = 0, east_m = 0, up_m = 0;
    bool up_valid = false;
    if (!flight::relative_north_east_up(own, observation, north_m, east_m, up_m, up_valid))
        return false;
    if (static_cast<int32_t>(hypotenuse(north_m, east_m)) > kAdsbGateRangeCeilingM) return true;
    return up_valid && std::abs(up_m) > kAdsbGateHeightCeilingM;
}

}  // namespace skyblip::traffic
