#include "protocols/adsl/flight_state.h"

namespace skyblip::adsl {

namespace {

enum class G13Category : uint8_t {
    LightRotorcraft = 3,
    LighterThanAir = 5,
    Paraglider = 7,
    Parachutist = 8,
    Evtol = 9,
    Gyrocopter = 10,
    UasOpen = 11,
    UasSpecific = 12,
    UasCertified = 13,
    HeavyRotorcraft = 15,
    HangGlider = 16,
    Paramotor = 17,
};

}

FlightState state_from(uint8_t code) {
    switch (static_cast<FlightState>(code)) {
        case FlightState::Ground: return FlightState::Ground;
        case FlightState::Airborne: return FlightState::Airborne;
        case FlightState::Unknown: break;
    }
    return FlightState::Unknown;
}

bool flies_at_no_ground_speed(uint8_t aircraft_category) {
    switch (static_cast<G13Category>(aircraft_category)) {
        case G13Category::LightRotorcraft:
        case G13Category::LighterThanAir:
        case G13Category::Paraglider:
        case G13Category::Parachutist:
        case G13Category::Evtol:
        case G13Category::Gyrocopter:
        case G13Category::UasOpen:
        case G13Category::UasSpecific:
        case G13Category::UasCertified:
        case G13Category::HeavyRotorcraft:
        case G13Category::HangGlider:
        case G13Category::Paramotor: return true;
    }
    return false;
}

uint8_t announced_state(uint8_t code, uint8_t aircraft_category) {
    if (state_from(code) == FlightState::Ground && flies_at_no_ground_speed(aircraft_category))
        return static_cast<uint8_t>(FlightState::Unknown);
    return code;
}

}
