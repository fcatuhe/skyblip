#include "disciplines/discipline.h"

#include "core/model/aircraft_category.h"
#include "disciplines/soaring/soaring.h"

namespace skyblip::discipline {

using model::AircraftCategory;

Discipline discipline_of(uint8_t aircraft_category) {
    switch (static_cast<AircraftCategory>(aircraft_category)) {
        case AircraftCategory::Glider:
        case AircraftCategory::Paraglider:
        case AircraftCategory::HangGlider: return Discipline::Soaring;
        case AircraftCategory::Unknown:
        case AircraftCategory::LightFixedWing:
        case AircraftCategory::HeavyFixedWing:
        case AircraftCategory::LightRotorcraft:
        case AircraftCategory::LighterThanAir:
        case AircraftCategory::Ultralight:
        case AircraftCategory::Parachutist:
        case AircraftCategory::Evtol:
        case AircraftCategory::Gyrocopter:
        case AircraftCategory::UasOpen:
        case AircraftCategory::UasSpecific:
        case AircraftCategory::UasCertified:
        case AircraftCategory::ModelPlane:
        case AircraftCategory::HeavyRotorcraft:
        case AircraftCategory::Paramotor: return Discipline::None;
    }
    return Discipline::None;
}

traffic::Level grade(const model::Ownship& own, const model::AircraftObservation& target,
                     uint32_t now_ms) {
    switch (discipline_of(own.aircraft_category)) {
        case Discipline::Soaring: return soaring::grade(own, target, now_ms);
        case Discipline::None: return traffic::Level::None;
    }
    return traffic::Level::None;
}

}  // namespace skyblip::discipline
