#ifndef SKYBLIP_PROTOCOLS_ADSL_FLIGHT_STATE_H
#define SKYBLIP_PROTOCOLS_ADSL_FLIGHT_STATE_H

#include <cstdint>

namespace skyblip::adsl {

// INFO: fc 18sep26 ADS-L 4 SRD860 issue 2 G.1.2 codes, the wire values themselves
enum class FlightState : uint8_t { Unknown = 0, Ground = 1, Airborne = 2 };

FlightState state_from(uint8_t code);

constexpr uint32_t kAirborneReportPeriodS = 1;
constexpr uint32_t kGroundReportPeriodS = 10;

// INFO: fc 21sep26 G.1.16 asks for at least 1 Hz airborne and 0.1 Hz on the ground
constexpr uint32_t report_period_s(FlightState state) {
    return state == FlightState::Ground ? kGroundReportPeriodS : kAirborneReportPeriodS;
}

bool flies_at_no_ground_speed(uint8_t aircraft_category);

// INFO: fc 23sep26 G.1.2's Undefined: no ground speed shows that a craft able to hover has landed
uint8_t announced_state(uint8_t code, uint8_t aircraft_category);
inline bool reduced_rate(uint8_t announced) { return state_from(announced) == FlightState::Ground; }

}

#endif
