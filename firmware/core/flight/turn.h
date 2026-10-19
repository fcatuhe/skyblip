// core/flight/turn.h: how fast a track is swinging, from two reported tracks
// and the gap between them. Own-ship differentiates its own GNSS track for the
// six-pack and for the extrapolation the transmitter applies; the alarm
// differentiates a target's track history to tell a gaggle from a collision
// course. Same arithmetic, two callers, one place: cordic9 is 512 units to the
// turn and the difference has to be taken the short way round, or a heading
// that crosses north reads as a 358 deg/s reversal.
#ifndef SKYBLIP_CORE_FLIGHT_TURN_H
#define SKYBLIP_CORE_FLIGHT_TURN_H

#include <cstdint>

#include "core/units/units.h"

namespace skyblip::flight {

constexpr int32_t kTrackC9Turn = 512;
constexpr uint16_t kTrackC9Mask = 0x1FF;
constexpr uint32_t kTurnWindowMs = 1000;

// The short way from reference to track, in cordic9 units: -256 (a half turn left)
// through +255, positive to the right.
int16_t track_delta_c9(uint16_t track_c9, uint16_t reference_track_c9);

// The short way from reference to track in hundredths of a degree, which is what
// own-ship's own track resolves and what a gyroscope is trimmed against: taken
// in whole degrees instead, a standard rate turn reads 2 deg/s.
int32_t track_delta_cdeg(CentiDegrees track, CentiDegrees reference_track);
int32_t turn_rate_cdps(CentiDegrees track, CentiDegrees reference_track, uint32_t elapsed_ms);

// Degrees per second, positive to the right. Zero for a zero interval: nothing
// happened in no time.
int16_t turn_rate_dps(uint16_t track_c9, uint16_t reference_track_c9, uint32_t elapsed_ms);

}  // namespace skyblip::flight

#endif
