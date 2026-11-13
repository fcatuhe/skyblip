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
// INFO: fc 28sep26 §C.2 at 100 kchip/s: 16 preamble + 64 sync + 24 bytes Manchester, 4.64 ms
constexpr uint32_t kAirTimeMs = 5;
// INFO: fc 13sep26 G.1.16 nav age, to the top of the transmit second: the burst is extrapolated
constexpr int32_t kFixMaxAgeMs = 500;
// INFO: fc 13nov26 G.2.11 asks for a Status at least once every 20 s
constexpr uint32_t kStatusPeriodS = 20;

inline uint32_t period_s(bool airborne) {
    return report_period_s(airborne ? FlightState::Airborne : FlightState::Ground);
}

// INFO: fc 13nov26 which second of the ground period a unit owns is the integrator's to choose
inline bool on_schedule(uint32_t utc_s, bool airborne, uint32_t ground_second) {
    return utc_s % period_s(airborne) == (airborne ? 0u : ground_second);
}

// INFO: fc 16sep26 §C.2.5 alternates the channel per transmission, and the clock counts them
inline int channel_in(uint32_t utc_s, bool airborne) {
    return static_cast<int>((utc_s / period_s(airborne)) & 1u);
}

}

#endif
