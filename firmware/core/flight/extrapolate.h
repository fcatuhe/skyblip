// core/flight/extrapolate.h: where own-ship is at an instant that is not the
// instant the fix is from. A burst leaves the radio somewhere in the direct
// slot, the solution behind it was solved when the receiver last spoke, and at
// 50 m/s every 100 ms between those two is 5 m of lie in a position other
// aircraft compute separation against. The transmitted timestamp names an
// instant; this is what makes the transmitted position belong to it.
#ifndef SKYBLIP_CORE_FLIGHT_EXTRAPOLATE_H
#define SKYBLIP_CORE_FLIGHT_EXTRAPOLATE_H

#include <cstdint>

#include "core/model/aircraft.h"
#include "core/model/ownship.h"

namespace skyblip::flight {

// INFO: rs 03aug26 The model is OGN's, in
// oss/nrf52-ogn-tracker src/ogn.h:1735-1790: constant ground speed on a
// constant turn rate, the turn applied as half before the velocity is resolved
// and half after, plus a constant climb. Half and half is what keeps a circling
// glider on its arc instead of on the tangent, at the cost of one sine.
//
// Beyond this gap it stops being a prediction. Our own §G.1.16 rule already
// refuses a navigation solution older than 500 ms, and the burst can leave up
// to a slot later than the attempt was decided, so the honest ceiling is a
// little over a second. Past it the last fix goes out dated as the last fix:
// wrong by a known amount beats wrong by an invented one.
constexpr int32_t kExtrapolationCeilingMs = 1500;

// Where the model says own-ship is at the target instant. When `valid` is
// false the fields are the unextrapolated fix, so a caller that only wants a
// position always has one.
struct Prediction {
    int32_t latitude_1e7{0};
    int32_t longitude_1e7{0};
    int32_t altitude_hae_mm{0};
    int32_t altitude_msl_mm{0};
    int32_t track_cdeg{0};
    bool valid{false};
};

// Pure: own-ship state and a signed offset from the instant that state is from.
// A negative offset predicts backwards, which is how the residual is measured.
Prediction extrapolate(const model::Ownship& own, int32_t dt_ms);

// INFO: fc 13sep26 a neighbour's burst carries position, speed and track, and no turn rate
Prediction extrapolate(const model::AircraftObservation& observation, int32_t dt_ms);

// INFO: fc 13sep26 a separation only means something between two states carried to one instant
model::Ownship carried_to(const model::Ownship& own, uint32_t now_ms);
model::AircraftObservation carried_to(const model::AircraftObservation& observation,
                                      uint32_t now_ms);

// INFO: rs 03aug26 OGN's PredResid (src/ogn.h:1424-1430): predict the previous
// position forward to the instant the next one arrived and sum the absolute
// north, east and vertical misses in metres. It is the only live measure of
// whether the model above is describing this aircraft or fighting it, and it
// costs one extrapolation per fix.
uint32_t prediction_residual_m(const Prediction& predicted, int32_t latitude_1e7,
                               int32_t longitude_1e7, int32_t altitude_hae_mm);

bool relative_ned(const model::Ownship& own, const model::AircraftObservation& target,
                  int32_t& north_m, int32_t& east_m, int32_t& up_m);

}  // namespace skyblip::flight

#endif
