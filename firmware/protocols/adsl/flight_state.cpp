#include "protocols/adsl/flight_state.h"

namespace skyblip::adsl {

FlightState state_from(uint8_t code) {
    switch (static_cast<FlightState>(code)) {
        case FlightState::Ground: return FlightState::Ground;
        case FlightState::Airborne: return FlightState::Airborne;
        case FlightState::Unknown: break;
    }
    return FlightState::Unknown;
}

}
