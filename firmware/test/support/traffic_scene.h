// Harness, not a test: own-ship in the air, the aircraft around it, and the reports they send.
#ifndef SKYBLIP_TEST_SUPPORT_TRAFFIC_SCENE_H
#define SKYBLIP_TEST_SUPPORT_TRAFFIC_SCENE_H

#include "core/flight/state.h"
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/units/units.h"
#include "core/util/intmath.h"

namespace skyblip {

inline model::Ownship flying(int speed_mps, int track_deg, int16_t turn_dps = 0,
                             uint32_t at_ms = 0) {
    model::Ownship o{};
    o.turn_cdps = static_cast<int16_t>(turn_dps * 100);
    o.fix_at_ms = at_ms;
    o.fix_valid = true;
    o.latitude_1e7 = 481000000;
    o.longitude_1e7 = 81000000;
    o.altitude_hae_mm = 1000000;
    o.speed_mm_s = speed_mps * 1000;
    o.track_cdeg = track_deg * 100;
    return o;
}

// Placed by offset from own-ship, so a case reads as the picture out of the
// canopy rather than as two coordinates.
inline model::AircraftObservation neighbour(const model::Ownship& own, int north_m, int east_m,
                                            int up_m, int speed_mps, int track_deg,
                                            uint32_t at_ms = 0) {
    model::AircraftObservation t{};
    t.address = 0x314159;
    t.address_mapping_table = 6;
    t.position_valid = true;
    t.speed_valid = true;
    t.speed_q = static_cast<uint16_t>(speed_mps * 4);
    t.track_c9 = to_cordic9(CentiDegrees(track_deg * 100)).v;
    t.altitude_m = to_metres(Millimetres(own.altitude_hae_mm)).v + up_m;
    t.latitude_1e7 =
        own.latitude_1e7 + static_cast<int32_t>(static_cast<int64_t>(north_m) * 1000000 / 11132);
    const int16_t ang =
        static_cast<int16_t>((static_cast<int64_t>(own.latitude_1e7) * 65536) / 3600000000LL);
    const int64_t east_scaled = static_cast<int64_t>(east_m) * kTrigOne / icos(ang);
    t.longitude_1e7 = own.longitude_1e7 + static_cast<int32_t>(east_scaled * 1000000 / 11132);
    t.received.at_s = at_ms / 1000;
    t.received.into_ms = static_cast<uint16_t>(at_ms % 1000);
    t.at_ms = at_ms;
    return t;
}

inline model::AircraftObservation observation(uint32_t address, uint8_t address_mapping_table,
                                              uint32_t at_s,
                                              model::Source source = model::Source::AdslDirect) {
    model::AircraftObservation o{};
    o.address = address;
    o.address_mapping_table = address_mapping_table;
    o.received.at_s = at_s;
    o.source = source;
    o.position_valid = true;
    o.latitude_1e7 = 481000000;
    o.longitude_1e7 = 81000000;
    o.altitude_m = 1000;
    return o;
}

inline model::AircraftObservation parked(uint32_t address, uint8_t address_mapping_table,
                                         uint32_t at_s,
                                         model::Source source = model::Source::AdslDirect) {
    model::AircraftObservation o = observation(address, address_mapping_table, at_s, source);
    o.flight_state = static_cast<uint8_t>(flight::FlightState::Ground);
    return o;
}

}  // namespace skyblip

#endif
