#include "core/flight/extrapolate.h"

#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/units/units.h"
#include "core/util/intmath.h"

namespace skyblip::flight {

namespace {

constexpr int64_t kTrigOne = 16384;
constexpr int64_t kTurn16 = 65536;
constexpr int64_t kCentiDegreeMsPerTurn = 36000000;
// The tree's one figure for the size of the earth, as core/protocol/nmea_out
// carries it: 1e-7 degree of latitude is 11132 micrometres.
constexpr int64_t kMicrometresPerE7 = 11132;
constexpr int64_t kMicrometresPerMetre = 1000000;
constexpr int64_t kMicrometresPerMillimetre = 1000;
constexpr int64_t kMsPerS = 1000;
// Below this cosine a metre of easting is more than a degree of longitude and
// the scaling stops meaning anything. 88 degrees of latitude is 500 km further
// north than anything this device will fly over.
constexpr int64_t kMinLatCosine = 512;

int64_t iabs64(int64_t v) { return v < 0 ? -v : v; }

int16_t lat_angle16(int32_t lat_1e7) {
    return static_cast<int16_t>((static_cast<int64_t>(lat_1e7) * kTurn16) / kE7PerTurn);
}

int64_t lat_cosine(int32_t lat_1e7) {
    const int64_t c = icos(lat_angle16(lat_1e7));
    return c < kMinLatCosine ? kMinLatCosine : c;
}

// Two angle units meet in this file: hundredths of a degree, which is what the
// instruments resolve, and the 16-bit cordic the sine table is indexed by.
int32_t turn_angle16(int16_t turn_cdps, int32_t dt_ms) {
    return static_cast<int32_t>(
        div_round(static_cast<int64_t>(turn_cdps) * dt_ms * kTurn16, kCentiDegreeMsPerTurn));
}

struct Motion {
    int32_t lat_1e7;
    int32_t lon_1e7;
    int32_t alt_mm;
    int32_t alt_msl_mm;
    int32_t speed_mm_s;
    int32_t track_cdeg;
    int32_t climb_mm_s;
    int16_t turn_cdps;
    bool known;
    bool climbs;
};

Prediction carry(const Motion& m, int32_t dt_ms) {
    Prediction out{};
    out.lat_1e7 = m.lat_1e7;
    out.lon_1e7 = m.lon_1e7;
    out.alt_mm = m.alt_mm;
    out.alt_msl_mm = m.alt_msl_mm;
    out.track_cdeg = wrapped(CentiDegrees(m.track_cdeg)).v;
    out.valid = false;

    if (!m.known) return out;
    if (dt_ms > kMaxExtrapolationMs || dt_ms < -kMaxExtrapolationMs) return out;
    out.valid = true;
    if (dt_ms == 0) return out;

    const int32_t turn16 = turn_angle16(m.turn_cdps, dt_ms);
    const int16_t heading = static_cast<int16_t>(
        static_cast<uint16_t>(to_angle16(CentiDegrees(m.track_cdeg)) + turn16 / 2));

    const int64_t scale = kTrigOne * kMicrometresPerE7;
    const int64_t travel = div_round<int64_t>(
        static_cast<int64_t>(m.speed_mm_s) * dt_ms * kMicrometresPerMillimetre, kMsPerS);
    out.lat_1e7 = m.lat_1e7 + static_cast<int32_t>(div_round(travel * icos(heading), scale));
    const int64_t east = div_round(travel * isin(heading), scale);
    out.lon_1e7 = wrapped_lon_1e7(static_cast<int64_t>(m.lon_1e7) +
                                  div_round(east * kTrigOne, lat_cosine(m.lat_1e7)));

    if (m.climbs) {
        const int32_t rise =
            static_cast<int32_t>(div_round(static_cast<int64_t>(m.climb_mm_s) * dt_ms, kMsPerS));
        out.alt_mm = m.alt_mm + rise;
        out.alt_msl_mm = m.alt_msl_mm + rise;
    }

    out.track_cdeg = centi_degrees_of_angle16(
                         static_cast<uint16_t>(to_angle16(CentiDegrees(m.track_cdeg)) + turn16))
                         .v;
    return out;
}

}  // namespace

Prediction extrapolate(const model::OwnState& own, int32_t dt_ms) {
    Motion m{};
    m.lat_1e7 = own.lat_1e7;
    m.lon_1e7 = own.lon_1e7;
    m.alt_mm = own.alt_mm;
    m.alt_msl_mm = own.alt_msl_mm;
    m.speed_mm_s = own.speed_mm_s;
    m.track_cdeg = own.track_cdeg;
    m.climb_mm_s = own.climb_mm_s;
    m.turn_cdps = own.turn_cdps;
    m.known = own.fix_valid;
    m.climbs = own.climb_valid;
    return carry(m, dt_ms);
}

// INFO: fc 13sep26 ADS-L carries no turn rate, so a neighbour is carried straight (G.1.8, G.1.10)
Prediction extrapolate(const model::AircraftObs& obs, int32_t dt_ms) {
    Motion m{};
    m.lat_1e7 = obs.lat_1e7;
    m.lon_1e7 = obs.lon_1e7;
    m.alt_mm = to_millimetres(Metres(obs.alt_m)).v;
    m.alt_msl_mm = m.alt_mm;
    m.speed_mm_s = to_mm_s(QuarterMetresPerSec(obs.speed_q)).v;
    m.track_cdeg = to_centi_degrees(Cordic9(obs.track_c9)).v;
    m.climb_mm_s = to_mm_s(EighthMetresPerSec(obs.climb_e8)).v;
    m.known = obs.position_valid && obs.speed_valid;
    m.climbs = obs.climb_valid;
    return carry(m, dt_ms);
}

model::OwnState carried_to(const model::OwnState& own, uint32_t now_ms) {
    const Prediction p = extrapolate(own, static_cast<int32_t>(now_ms - own.fix_ms));
    model::OwnState out = own;
    out.lat_1e7 = p.lat_1e7;
    out.lon_1e7 = p.lon_1e7;
    out.alt_mm = p.alt_mm;
    out.alt_msl_mm = p.alt_msl_mm;
    out.track_cdeg = p.track_cdeg;
    return out;
}

model::AircraftObs carried_to(const model::AircraftObs& obs, uint32_t now_ms) {
    const Prediction p = extrapolate(obs, static_cast<int32_t>(now_ms - obs.at_ms));
    model::AircraftObs out = obs;
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
