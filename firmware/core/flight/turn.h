#ifndef SKYBLIP_CORE_FLIGHT_TURN_H
#define SKYBLIP_CORE_FLIGHT_TURN_H

#include <cstdint>

#include "core/units/units.h"

namespace skyblip::flight {

constexpr int32_t kTrackC9Turn = 512;
constexpr uint16_t kTrackC9Mask = 0x1FF;
constexpr uint32_t kTurnWindowMs = 1000;

// INFO: fc 20nov26 the short way, -256 to +255, or crossing north reads as a 358 deg/s reversal
int16_t track_delta_c9(uint16_t track_c9, uint16_t reference_track_c9);

// INFO: fc 20nov26 hundredths, because whole degrees read a standard rate turn as 2 deg/s
int32_t track_delta_cdeg(CentiDegrees track, CentiDegrees reference_track);
int32_t turn_rate_cdps(CentiDegrees track, CentiDegrees reference_track, uint32_t elapsed_ms);

// INFO: fc 20nov26 positive to the right, and 0 over a zero interval
int16_t turn_rate_dps(uint16_t track_c9, uint16_t reference_track_c9, uint32_t elapsed_ms);

}  // namespace skyblip::flight

#endif
