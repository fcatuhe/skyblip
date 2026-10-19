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
constexpr int64_t kMinLatCosine = 512;

int64_t iabs64(int64_t v) { return v < 0 ? -v : v; }

int16_t lat_angle16(int32_t lat_1e7) {
    return static_cast<int16_t>((static_cast<int64_t>(lat_1e7) * kAngle16PerTurn) / kE7PerTurn);
}

int64_t lat_cosine(int32_t lat_1e7) {
    const int64_t c = icos(lat_angle16(lat_1e7));
    return c < kMinLatCosine ? kMinLatCosine : c;
}

struct Carried {
    int32_t lat_1e7;
    int32_t lon_1e7;
    int32_t alt_mm;
    int32_t alt_msl_mm;
    int32_t speed_mm_s;
    int32_t track_cdeg;
    int32_t climb_mm_s;
    int16_t turn_cdps;
    bool fix_valid;
    bool climb_valid;
};

Prediction carry(const Carried& motion, int32_t dt_ms) {
    Prediction out{};
    out.lat_1e7 = motion.lat_1e7;
    out.lon_1e7 = motion.lon_1e7;
    out.alt_mm = motion.alt_mm;
    out.alt_msl_mm = motion.alt_msl_mm;
    out.track_cdeg = wrapped(CentiDegrees(motion.track_cdeg)).v;
    out.valid = false;

    if (!motion.fix_valid) return out;
    if (dt_ms > kMaxExtrapolationMs || dt_ms < -kMaxExtrapolationMs) return out;
    out.valid = true;
    if (dt_ms == 0) return out;

    const int32_t turn16 = turn_angle16(motion.turn_cdps, dt_ms);
    const int16_t heading = static_cast<int16_t>(
        static_cast<uint16_t>(to_angle16(CentiDegrees(motion.track_cdeg)) + turn16 / 2));

    const int64_t scale = kTrigOne * kMicrometresPerE7;
    const int64_t travel = div_round<int64_t>(
        static_cast<int64_t>(motion.speed_mm_s) * dt_ms * kMicrometresPerMillimetre,
        kMillisecondsPerSecond);
    out.lat_1e7 = motion.lat_1e7 + static_cast<int32_t>(div_round(travel * icos(heading), scale));
    const int64_t east = div_round(travel * isin(heading), scale);
    out.lon_1e7 = wrapped_lon_1e7(static_cast<int64_t>(motion.lon_1e7) +
                                  div_round(east * kTrigOne, lat_cosine(motion.lat_1e7)));

    if (motion.climb_valid) {
        const int32_t rise = static_cast<int32_t>(div_round<int64_t>(
            static_cast<int64_t>(motion.climb_mm_s) * dt_ms, kMillisecondsPerSecond));
        out.alt_mm = motion.alt_mm + rise;
        out.alt_msl_mm = motion.alt_msl_mm + rise;
    }

    out.track_cdeg =
        centi_degrees_of_angle16(
            static_cast<uint16_t>(to_angle16(CentiDegrees(motion.track_cdeg)) + turn16))
            .v;
    return out;
}

}  // namespace

Prediction extrapolate(const model::Ownship& own, int32_t dt_ms) {
    Carried motion{};
    motion.lat_1e7 = own.lat_1e7;
    motion.lon_1e7 = own.lon_1e7;
    motion.alt_mm = own.alt_mm;
    motion.alt_msl_mm = own.alt_msl_mm;
    motion.speed_mm_s = own.speed_mm_s;
    motion.track_cdeg = own.track_cdeg;
    motion.climb_mm_s = own.climb_mm_s;
    motion.turn_cdps = own.turn_cdps;
    motion.fix_valid = own.fix_valid;
    motion.climb_valid = own.climb_valid;
    return carry(motion, dt_ms);
}

// INFO: fc 13sep26 ADS-L carries no turn rate, so a neighbour is carried straight (G.1.8, G.1.10)
Prediction extrapolate(const model::AircraftObservation& observation, int32_t dt_ms) {
    Carried motion{};
    motion.lat_1e7 = observation.lat_1e7;
    motion.lon_1e7 = observation.lon_1e7;
    motion.alt_mm = to_millimetres(Metres(observation.alt_m)).v;
    motion.alt_msl_mm = motion.alt_mm;
    motion.speed_mm_s = to_mm_s(QuarterMetresPerSec(observation.speed_q)).v;
    motion.track_cdeg = to_centi_degrees(Cordic9(observation.track_c9)).v;
    motion.climb_mm_s = to_mm_s(EighthMetresPerSec(observation.climb_e8)).v;
    motion.fix_valid = observation.position_valid && observation.speed_valid;
    motion.climb_valid = observation.climb_valid;
    return carry(motion, dt_ms);
}

model::Ownship carried_to(const model::Ownship& own, uint32_t now_ms) {
    const Prediction p = extrapolate(own, static_cast<int32_t>(now_ms - own.fix_ms));
    model::Ownship out = own;
    out.lat_1e7 = p.lat_1e7;
    out.lon_1e7 = p.lon_1e7;
    out.alt_mm = p.alt_mm;
    out.alt_msl_mm = p.alt_msl_mm;
    out.track_cdeg = p.track_cdeg;
    return out;
}

model::AircraftObservation carried_to(const model::AircraftObservation& observation,
                                      uint32_t now_ms) {
    const Prediction p = extrapolate(observation, static_cast<int32_t>(now_ms - observation.at_ms));
    model::AircraftObservation out = observation;
    out.lat_1e7 = p.lat_1e7;
    out.lon_1e7 = p.lon_1e7;
    out.alt_m = to_metres(Millimetres(p.alt_mm)).v;
    return out;
}

uint32_t prediction_residual_m(const Prediction& predicted, int32_t lat_1e7, int32_t lon_1e7,
                               int32_t alt_mm) {
    const int64_t dlat = static_cast<int64_t>(lat_1e7) - predicted.lat_1e7;
    const int64_t dlon = wrapped_lon_1e7(static_cast<int64_t>(lon_1e7) - predicted.lon_1e7);
    const int64_t north = div_round(dlat * kMicrometresPerE7, kMicrometresPerMetre);
    const int64_t east = div_round(dlon * kMicrometresPerE7 * lat_cosine(predicted.lat_1e7),
                                   kMicrometresPerMetre * kTrigOne);
    const int64_t up =
        div_round<int64_t>(static_cast<int64_t>(alt_mm) - predicted.alt_mm, kMillimetresPerMetre);
    return static_cast<uint32_t>(iabs64(north) + iabs64(east) + iabs64(up));
}

}  // namespace skyblip::flight
