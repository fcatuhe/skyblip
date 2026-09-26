#ifndef SKYBLIP_CORE_FLIGHT_GROUND_H
#define SKYBLIP_CORE_FLIGHT_GROUND_H

#include "core/flight/state.h"

namespace skyblip::flight {

class GroundLatch {
   public:
    void update(FlightState reported);

    FlightState state() const { return state_; }
    bool on_ground() const { return state_ == FlightState::Ground; }

   private:
    FlightState state_{FlightState::Unknown};
    bool airborne_latched_{false};
};

}  // namespace skyblip::flight

#endif
