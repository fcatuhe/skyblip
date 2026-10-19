#include "core/flight/extrapolate.h"

#include "core/flight/arc.h"
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/units/units.h"
#include "core/util/intmath.h"

namespace skyblip::flight {

namespace {

// The tree's one figure for the size of the earth, as core/protocol/nmea_out
// carries it: 1e-7 degree of latitude is 11132 micrometres.
constexpr int64_t kMicrometresPerE7 = 11132;
constexpr int64_t kMicrometresPerMetre = 1000000;
constexpr int64_t kMicrometresPerMillimetre = 1000;
// Below this cosine a metre of easting is more than a degree of longitude and
// the scaling stops meaning anything. 88 degrees of latitude is 500 km further
// north than anything this device will fly over.
constexpr int64_t kLatitudeCosineFloor = 512;

int64_t absolute(int64_t value) { return value < 0 ? -value : value; }

int16_t latitude_angle16(int32_t latitude_1e7) {
    return static_cast<int16_t>((static_cast<int64_t>(latitude_1e7) * kAngle16PerTurn) /
                                kE7PerTurn);
}

int64_t latitude_cosine(int32_t latitude_1e7) {
    const int64_t cosine = icos(latitude_angle16(latitude_1e7));
    return cosine < kLatitudeCosineFloor ? kLatitudeCosineFloor : cosine;
}

struct Carried {
    int32_t latitude_1e7;
    int32_t longitude_1e7;
    int32_t altitude_hae_mm;
    int32_t altitude_msl_mm;
    int32_t speed_mm_s;
    int32_t track_cdeg;
    int32_t climb_mm_s;
    int16_t turn_cdps;
    bool fix_valid;
    bool climb_valid;
};

Prediction carry(const Carried& motion, int32_t elapsed_ms) {
    Prediction out{};
    out.latitude_1e7 = motion.latitude_1e7;
    out.longitude_1e7 = motion.longitude_1e7;
    out.altitude_hae_mm = motion.altitude_hae_mm;
    out.altitude_msl_mm = motion.altitude_msl_mm;
    out.track_cdeg = wrapped(CentiDegrees(motion.track_cdeg)).v;
    out.valid = false;

    if (!motion.fix_valid) return out;
    if (elapsed_ms > kExtrapolationCeilingMs || elapsed_ms < -kExtrapolationCeilingMs) return out;
    out.valid = true;
    if (elapsed_ms == 0) return out;

    const int32_t turn_angle = turn_angle16(motion.turn_cdps, elapsed_ms);
    const int16_t heading = static_cast<int16_t>(
        static_cast<uint16_t>(to_angle16(CentiDegrees(motion.track_cdeg)) + turn_angle / 2));

    const int64_t scale = kTrigOne * kMicrometresPerE7;
    const int64_t travel = div_round<int64_t>(
        static_cast<int64_t>(motion.speed_mm_s) * elapsed_ms * kMicrometresPerMillimetre,
        kMillisecondsPerSecond);
    out.latitude_1e7 =
        motion.latitude_1e7 + static_cast<int32_t>(div_round(travel * icos(heading), scale));
    const int64_t east = div_round(travel * isin(heading), scale);
    out.longitude_1e7 =
        wrapped_longitude_1e7(static_cast<int64_t>(motion.longitude_1e7) +
                              div_round(east * kTrigOne, latitude_cosine(motion.latitude_1e7)));

    if (motion.climb_valid) {
        const int32_t rise = static_cast<int32_t>(div_round<int64_t>(
            static_cast<int64_t>(motion.climb_mm_s) * elapsed_ms, kMillisecondsPerSecond));
        out.altitude_hae_mm = motion.altitude_hae_mm + rise;
        out.altitude_msl_mm = motion.altitude_msl_mm + rise;
    }

    out.track_cdeg =
        centi_degrees_of_angle16(
            static_cast<uint16_t>(to_angle16(CentiDegrees(motion.track_cdeg)) + turn_angle))
            .v;
    return out;
}

}  // namespace

Prediction extrapolate(const model::Ownship& own, int32_t elapsed_ms) {
    Carried motion{};
    motion.latitude_1e7 = own.latitude_1e7;
    motion.longitude_1e7 = own.longitude_1e7;
    motion.altitude_hae_mm = own.altitude_hae_mm;
    motion.altitude_msl_mm = own.altitude_msl_mm;
    motion.speed_mm_s = own.speed_mm_s;
    motion.track_cdeg = own.track_cdeg;
    motion.climb_mm_s = own.climb_mm_s;
    motion.turn_cdps = own.turn_cdps;
    motion.fix_valid = own.fix_valid;
    motion.climb_valid = own.climb_valid;
    return carry(motion, elapsed_ms);
}

// INFO: fc 13sep26 ADS-L carries no turn rate, so a neighbour is carried straight (G.1.8, G.1.10)
Prediction extrapolate(const model::AircraftObservation& observation, int32_t elapsed_ms) {
    Carried motion{};
    motion.latitude_1e7 = observation.latitude_1e7;
    motion.longitude_1e7 = observation.longitude_1e7;
    motion.altitude_hae_mm = to_millimetres(Metres(observation.altitude_m)).v;
    motion.altitude_msl_mm = motion.altitude_hae_mm;
    motion.speed_mm_s = to_mm_s(QuarterMetresPerSecond(observation.speed_q)).v;
    motion.track_cdeg = to_centi_degrees(Cordic9(observation.track_c9)).v;
    motion.climb_mm_s = to_mm_s(EighthMetresPerSecond(observation.climb_e8)).v;
    motion.fix_valid = observation.position_valid && observation.speed_valid;
    motion.climb_valid = observation.climb_valid;
    return carry(motion, elapsed_ms);
}

model::Ownship carried_to(const model::Ownship& own, uint32_t now_ms) {
    const Prediction prediction = extrapolate(own, static_cast<int32_t>(now_ms - own.fix_at_ms));
    model::Ownship out = own;
    out.latitude_1e7 = prediction.latitude_1e7;
    out.longitude_1e7 = prediction.longitude_1e7;
    out.altitude_hae_mm = prediction.altitude_hae_mm;
    out.altitude_msl_mm = prediction.altitude_msl_mm;
    out.track_cdeg = prediction.track_cdeg;
    return out;
}

model::AircraftObservation carried_to(const model::AircraftObservation& observation,
                                      uint32_t now_ms) {
    const Prediction prediction =
        extrapolate(observation, static_cast<int32_t>(now_ms - observation.at_ms));
    model::AircraftObservation out = observation;
    out.latitude_1e7 = prediction.latitude_1e7;
    out.longitude_1e7 = prediction.longitude_1e7;
    out.altitude_m = to_metres(Millimetres(prediction.altitude_hae_mm)).v;
    return out;
}

uint32_t prediction_residual_m(const Prediction& predicted, int32_t latitude_1e7,
                               int32_t longitude_1e7, int32_t altitude_hae_mm) {
    const int64_t latitude_delta_1e7 = static_cast<int64_t>(latitude_1e7) - predicted.latitude_1e7;
    const int64_t longitude_delta_1e7 =
        wrapped_longitude_1e7(static_cast<int64_t>(longitude_1e7) - predicted.longitude_1e7);
    const int64_t north = div_round(latitude_delta_1e7 * kMicrometresPerE7, kMicrometresPerMetre);
    const int64_t east =
        div_round(longitude_delta_1e7 * kMicrometresPerE7 * latitude_cosine(predicted.latitude_1e7),
                  kMicrometresPerMetre * kTrigOne);
    const int64_t up = div_round<int64_t>(
        static_cast<int64_t>(altitude_hae_mm) - predicted.altitude_hae_mm, kMillimetresPerMetre);
    return static_cast<uint32_t>(absolute(north) + absolute(east) + absolute(up));
}

bool relative_north_east_up(const model::Ownship& own, const model::AircraftObservation& target,
                            int32_t& north_m, int32_t& east_m, int32_t& up_m) {
    if (!own.fix_valid || !target.position_valid) return false;
    const int64_t latitude_delta_1e7 = static_cast<int64_t>(target.latitude_1e7) - own.latitude_1e7;
    const int64_t longitude_delta_1e7 =
        wrapped_longitude_1e7(static_cast<int64_t>(target.longitude_1e7) - own.longitude_1e7);
    north_m = static_cast<int32_t>(
        div_round<int64_t>(latitude_delta_1e7 * kMicrometresPerE7, kMicrometresPerMetre));
    const int64_t cosine = icos(latitude_angle16(own.latitude_1e7));
    const int64_t east_micrometres =
        div_round<int64_t>(longitude_delta_1e7 * kMicrometresPerE7 * cosine, kTrigOne);
    east_m = static_cast<int32_t>(div_round<int64_t>(east_micrometres, kMicrometresPerMetre));
    up_m = target.altitude_valid ? target.altitude_m - to_metres(Millimetres(own.altitude_hae_mm)).v
                                 : 0;
    return true;
}

}  // namespace skyblip::flight
