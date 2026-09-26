#include "core/flight/timer.h"

namespace skyblip::flight {

void FlightTimer::update(FlightState state, uint32_t now_ms) {
    if (state == FlightState::Unknown && !running_) return;

    if (state == FlightState::Ground) {
        running_ = false;
        return;
    }

    if (!running_) {
        running_ = true;
        flown_ = true;
        takeoff_ms_ = now_ms;
        carried_s_ = seconds_;
    }
    seconds_ = carried_s_ + (now_ms - takeoff_ms_) / 1000;
}

}  // namespace skyblip::flight
