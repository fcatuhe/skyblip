#ifndef SKYBLIP_DISCIPLINES_DISCIPLINE_H
#define SKYBLIP_DISCIPLINES_DISCIPLINE_H

#include <cstdint>

#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/traffic/alarm.h"

namespace skyblip::discipline {

enum class Discipline : uint8_t { None = 0, Soaring = 1 };

Discipline discipline_of(uint8_t aircraft_category);

traffic::Level grade(const model::Ownship& own, const model::AircraftObservation& target,
                     uint32_t now_ms);

}  // namespace skyblip::discipline

#endif
