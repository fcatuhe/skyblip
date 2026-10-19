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
    model::Ownship own{};
    own.turn_cdps = static_cast<int16_t>(turn_dps * 100);
    own.fix_at_ms = at_ms;
    own.fix_valid = true;
    own.latitude_1e7 = 481000000;
    own.longitude_1e7 = 81000000;
    own.altitude_hae_mm = 1000000;
    own.speed_mm_s = speed_mps * 1000;
    own.track_cdeg = track_deg * 100;
    return own;
}

// Placed by offset from own-ship, so a case reads as the picture out of the
// canopy rather than as two coordinates.
inline model::AircraftObservation neighbour(const model::Ownship& own, int north_m, int east_m,
                                            int up_m, int speed_mps, int track_deg,
                                            uint32_t at_ms = 0) {
    model::AircraftObservation observation{};
    observation.address = 0x314159;
    observation.address_mapping_table = 6;
    observation.position_valid = true;
    observation.speed_valid = true;
    observation.speed_q = static_cast<uint16_t>(speed_mps * 4);
    observation.track_c9 = to_cordic9(CentiDegrees(track_deg * 100)).value;
    observation.altitude_m = to_metres(Millimetres(own.altitude_hae_mm)).value + up_m;
    observation.latitude_1e7 =
        own.latitude_1e7 + static_cast<int32_t>(static_cast<int64_t>(north_m) * 1000000 / 11132);
    const int16_t latitude_angle =
        static_cast<int16_t>((static_cast<int64_t>(own.latitude_1e7) * 65536) / 3600000000LL);
    const int64_t east_scaled = static_cast<int64_t>(east_m) * kTrigOne / cosine(latitude_angle);
    observation.longitude_1e7 =
        own.longitude_1e7 + static_cast<int32_t>(east_scaled * 1000000 / 11132);
    observation.received.at_s = at_ms / 1000;
    observation.received.into_ms = static_cast<uint16_t>(at_ms % 1000);
    observation.at_ms = at_ms;
    return observation;
}

inline model::AircraftObservation observation(uint32_t address, uint8_t address_mapping_table,
                                              uint32_t at_s,
                                              model::Source source = model::Source::AdslDirect) {
    model::AircraftObservation observation{};
    observation.address = address;
    observation.address_mapping_table = address_mapping_table;
    observation.received.at_s = at_s;
    observation.source = source;
    observation.position_valid = true;
    observation.latitude_1e7 = 481000000;
    observation.longitude_1e7 = 81000000;
    observation.altitude_m = 1000;
    return observation;
}

inline model::AircraftObservation parked(uint32_t address, uint8_t address_mapping_table,
                                         uint32_t at_s,
                                         model::Source source = model::Source::AdslDirect) {
    model::AircraftObservation parked_observation =
        observation(address, address_mapping_table, at_s, source);
    parked_observation.flight_state = static_cast<uint8_t>(flight::FlightState::Ground);
    return parked_observation;
}

}  // namespace skyblip

#endif
