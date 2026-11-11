#include "core/traffic/sanity.h"

#include "core/flight/extrapolate.h"
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/traffic/range.h"
#include "core/util/intmath.h"

namespace skyblip::traffic {

int32_t plausible_range_m(model::Source source) {
    switch (source) {
        case model::Source::AdslUplink: return kRelayedRangeCeilingM;
        case model::Source::Adsb: return kAdsbRangeCeilingM;
        default: return kPlausibleRangeCeilingM;
    }
}

Plausibility range_check(const model::Ownship& own, const model::AircraftObservation& observation,
                         int32_t& slant_m) {
    int32_t north_m = 0, east_m = 0, up_m = 0;
    bool up_valid = false;
    if (!flight::relative_north_east_up(own, observation, north_m, east_m, up_m, up_valid))
        return Plausibility::NoReference;

    // INFO: fc 19sep26 slant, not ground: a bad altitude field throws a target straight up
    const int32_t ground_m = static_cast<int32_t>(hypotenuse(north_m, east_m));
    slant_m = up_valid ? static_cast<int32_t>(hypotenuse(ground_m, up_m)) : ground_m;
    return slant_m > plausible_range_m(observation.source) ? Plausibility::TooFar
                                                           : Plausibility::Believable;
}

}  // namespace skyblip::traffic
