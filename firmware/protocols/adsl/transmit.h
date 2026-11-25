#ifndef SKYBLIP_PROTOCOLS_ADSL_TRANSMIT_H
#define SKYBLIP_PROTOCOLS_ADSL_TRANSMIT_H

#include <cstdint>

#include "protocols/adsl/flight_state.h"

namespace skyblip::adsl {

// INFO: fc 13nov26 §C.5 gives the ground stations 200 to 450 ms and the aircraft 450 to 1000
constexpr int kUplinkSlotStartMs = 200;
constexpr int kUplinkSlotEndMs = 450;
constexpr int kDirectStartMs = 450;
constexpr int kDirectEndMs = 1000;
// INFO: fc 22nov26 §C.2 at 100 kchip/s: 32 chips before sync, 32 sync, 16 length and 384 packet
// make 4.64 ms
constexpr uint32_t kAirTimeMs = 5;
// INFO: fc 18nov26 G.1.16 nav age, from the PVT fix to the instant the burst goes on air
constexpr int32_t kFixMaxAgeMs = 500;
// INFO: fc 13nov26 G.2.11 asks for a Status at least once every 20 s
constexpr uint32_t kStatusPeriodS = 20;

inline uint32_t period_s(bool airborne) {
    return report_period_s(airborne ? FlightState::Airborne : FlightState::Ground);
}

}

#endif
