#ifndef SKYBLIP_CORE_TRAFFIC_SANITY_H
#define SKYBLIP_CORE_TRAFFIC_SANITY_H

#include <cstdint>

#include "core/model/aircraft.h"
#include "core/model/ownship.h"

namespace skyblip::traffic {

constexpr int32_t kPlausibleRangeCeilingM = 30000;

constexpr int32_t kRelayedRangeCeilingM = 2 * kPlausibleRangeCeilingM;

// INFO: fc 10nov26 1090 MHz line of sight between two aircraft at FL600, worked out in README.md
constexpr int32_t kAdsbRangeCeilingM = 1115000;

int32_t plausible_range_m(model::Source source);

enum class Plausibility : uint8_t { Believable, NoReference, TooFar };

Plausibility range_check(const model::Ownship& own, const model::AircraftObservation& observation,
                         int32_t& slant_m);

}  // namespace skyblip::traffic

#endif
