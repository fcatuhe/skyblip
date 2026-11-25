#ifndef SKYBLIP_CORE_MODEL_AIRCRAFT_CATEGORY_H
#define SKYBLIP_CORE_MODEL_AIRCRAFT_CATEGORY_H

#include <cstdint>

namespace skyblip::model {

enum class AircraftCategory : uint8_t {
    Unknown = 0,
    LightFixedWing = 1,
    HeavyFixedWing = 2,
    LightRotorcraft = 3,
    Glider = 4,
    LighterThanAir = 5,
    Ultralight = 6,
    Paraglider = 7,
    Parachutist = 8,
    Evtol = 9,
    Gyrocopter = 10,
    UasOpen = 11,
    UasSpecific = 12,
    UasCertified = 13,
    ModelPlane = 14,
    HeavyRotorcraft = 15,
    HangGlider = 16,
    Paramotor = 17,
};

}  // namespace skyblip::model

#endif
