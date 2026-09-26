// Harness, not a test: own-ship in the air, the aircraft around it, and the reports they send.
#ifndef SKYBLIP_TEST_SUPPORT_TRAFFIC_SCENE_H
#define SKYBLIP_TEST_SUPPORT_TRAFFIC_SCENE_H

#include "core/flight/state.h"
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/units/units.h"
#include "core/util/intmath.h"

namespace skyblip {

inline uint16_t c9(int deg) { return static_cast<uint16_t>(((deg % 360 + 360) % 360) * 512 / 360); }

inline model::OwnState flying(int mps, int track_deg, int16_t turn_dps = 0, uint32_t at_ms = 0) {
    model::OwnState o{};
    o.turn_cdps = static_cast<int16_t>(turn_dps * 100);
    o.fix_ms = at_ms;
    o.fix_valid = true;
    o.lat_1e7 = 481000000;
    o.lon_1e7 = 81000000;
    o.alt_mm = 1000000;
    o.speed_mm_s = mps * 1000;
    o.track_cdeg = track_deg * 100;
    return o;
}

// Placed by offset from own-ship, so a case reads as the picture out of the
// canopy rather than as two coordinates.
inline model::AircraftObs neighbour(const model::OwnState& own, int north_m, int east_m, int up_m,
                                    int mps, int track_deg, uint32_t at_ms = 0) {
    model::AircraftObs t{};
    t.addr = 0x314159;
    t.addr_table = 6;
    t.position_valid = true;
    t.speed_valid = true;
    t.speed_q = static_cast<uint16_t>(mps * 4);
    t.track_c9 = c9(track_deg);
    t.alt_m = to_metres(Millimetres(own.alt_mm)).v + up_m;
    t.lat_1e7 = own.lat_1e7 + static_cast<int32_t>(static_cast<int64_t>(north_m) * 1000000 / 11132);
    const int16_t ang =
        static_cast<int16_t>((static_cast<int64_t>(own.lat_1e7) * 65536) / 3600000000LL);
    const int64_t east_scaled = static_cast<int64_t>(east_m) * 16384 / icos(ang);
    t.lon_1e7 = own.lon_1e7 + static_cast<int32_t>(east_scaled * 1000000 / 11132);
    t.received.at_s = at_ms / 1000;
    t.received.into_ms = static_cast<uint16_t>(at_ms % 1000);
    t.at_ms = at_ms;
    return t;
}

inline model::AircraftObs obs(uint32_t addr, uint8_t tbl, uint32_t t,
                              model::Source src = model::Source::AdslDirect) {
    model::AircraftObs o{};
    o.addr = addr;
    o.addr_table = tbl;
    o.received.at_s = t;
    o.source = src;
    o.position_valid = true;
    o.lat_1e7 = 481000000;
    o.lon_1e7 = 81000000;
    o.alt_m = 1000;
    return o;
}

inline model::AircraftObs parked(uint32_t addr, uint8_t tbl, uint32_t t,
                                 model::Source src = model::Source::AdslDirect) {
    model::AircraftObs o = obs(addr, tbl, t, src);
    o.flight_state = static_cast<uint8_t>(flight::FlightState::Ground);
    return o;
}

}  // namespace skyblip

#endif
