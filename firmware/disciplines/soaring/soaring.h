#ifndef SKYBLIP_DISCIPLINES_SOARING_SOARING_H
#define SKYBLIP_DISCIPLINES_SOARING_SOARING_H

#include <cstdint>

#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/traffic/alarm.h"

namespace skyblip::discipline::soaring {

traffic::Level grade(const model::Ownship& own, const model::AircraftObservation& target,
                     uint32_t now_ms);

}  // namespace skyblip::discipline::soaring

#endif
