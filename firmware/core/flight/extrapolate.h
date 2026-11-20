#ifndef SKYBLIP_CORE_FLIGHT_EXTRAPOLATE_H
#define SKYBLIP_CORE_FLIGHT_EXTRAPOLATE_H

#include <cstdint>

#include "core/model/aircraft.h"
#include "core/model/ownship.h"

namespace skyblip::flight {

// INFO: rs 03aug26 past this the last fix stands where it was, never at an invented position
constexpr int32_t kExtrapolationCeilingMs = 1500;

// INFO: fc 20nov26 an invalid prediction holds the fix as it was, so a caller always has a position
struct Prediction {
    int32_t latitude_1e7{0};
    int32_t longitude_1e7{0};
    int32_t altitude_hae_mm{0};
    int32_t altitude_msl_mm{0};
    int32_t track_cdeg{0};
    bool valid{false};
};

// INFO: fc 20nov26 a negative offset predicts backwards, which is how the residual is measured
Prediction extrapolate(const model::Ownship& own, int32_t elapsed_ms);

// INFO: fc 13sep26 a neighbour's burst carries position, speed and track, and no turn rate
Prediction extrapolate(const model::AircraftObservation& observation, int32_t elapsed_ms);

// INFO: fc 13sep26 a separation only means something between two states carried to one instant
model::Ownship carried_to(const model::Ownship& own, uint32_t now_ms);
model::AircraftObservation carried_to(const model::AircraftObservation& observation,
                                      uint32_t now_ms);

// INFO: rs 03aug26 OGN's PredResid (src/ogn.h:1424-1430), the live measure of the model's fit
uint32_t prediction_residual_m(const Prediction& predicted, int32_t latitude_1e7,
                               int32_t longitude_1e7, int32_t altitude_hae_mm);

// INFO: fc 10nov26 up_m is 0 when up_valid is false, so each caller decides what unknown means
bool relative_north_east_up(const model::Ownship& own, const model::AircraftObservation& target,
                            int32_t& north_m, int32_t& east_m, int32_t& up_m, bool& up_valid);

bool gnss_relative_altitude_m(const model::Ownship& own, const model::AircraftObservation& target,
                              int32_t& up_m);
bool pressure_relative_altitude_m(const model::Ownship& own,
                                  const model::AircraftObservation& target, int32_t& up_m);

}  // namespace skyblip::flight

#endif
