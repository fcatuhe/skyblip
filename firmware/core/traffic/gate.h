#ifndef SKYBLIP_CORE_TRAFFIC_GATE_H
#define SKYBLIP_CORE_TRAFFIC_GATE_H

#include <cstdint>

#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/traffic/sanity.h"

namespace skyblip::traffic {

// INFO: fc 10nov26 no farther than a direct ADS-L or FLARM target can be (README.md)
constexpr int32_t kAdsbGateRangeCeilingM = kPlausibleRangeCeilingM;
// INFO: fc 10nov26 PowerFLARM's default VRANGE, FLARM FTD-014 version 1.18 3.1.6 (README.md)
constexpr int32_t kAdsbGateHeightCeilingM = 500;

bool outside_adsb_gate(const model::Ownship& own, const model::AircraftObservation& observation);

}  // namespace skyblip::traffic

#endif
