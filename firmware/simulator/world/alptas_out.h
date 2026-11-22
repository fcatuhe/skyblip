#ifndef SKYBLIP_SIMULATOR_WORLD_ALPTAS_OUT_H
#define SKYBLIP_SIMULATOR_WORLD_ALPTAS_OUT_H

#include <cstdint>

#include "core/flight/state.h"
#include "core/model/aircraft.h"
#include "core/protocol/nmea_out.h"
#include "core/util/result.h"
#include "protocols/adsl/packet.h"
#include "protocols/alptas/frame.h"
#include "protocols/common/geometry.h"

namespace skyblip::simulator {

inline uint8_t alptas_address_type_of(uint8_t address_mapping_table) {
    switch (address_mapping_table) {
        case adsl::kAddressTableIcao: return alptas::kAddressTypeIcao;
        case adsl::kAddressTableFlarm: return alptas::kAddressTypeManufacturer;
        default: return 0;
    }
}

inline Status alptas_encode(uint8_t* frame, const model::AircraftObservation& observation,
                            uint32_t utc_s) {
    if (!observation.position_valid) return Status::Invalid;
    alptas::Position position;
    position.address = observation.address;
    position.address_type = alptas_address_type_of(observation.address_mapping_table);
    position.aircraft_type = protocol::adsl_category_to_alptas_type(observation.aircraft_category);
    position.airborne = flight::on_ground(observation.flight_state) ? alptas::kAirborneOnGround
                                                                    : alptas::kAirborneFlying;
    position.latitude_1e7 = observation.latitude_1e7;
    position.longitude_1e7 = observation.longitude_1e7;
    position.altitude_m = observation.altitude_m;
    position.speed_e1 =
        observation.speed_valid ? wire::divide_rounded<int32_t>(observation.speed_q * 5, 2) : 0;
    position.climb_e1 =
        observation.climb_valid ? wire::divide_rounded<int32_t>(observation.climb_e8 * 5, 4) : 0;
    position.track_deg = static_cast<uint16_t>(
        static_cast<uint32_t>(wire::divide_rounded<int32_t>(observation.track_c9 * 360, 512)) %
        360u);
    position.urgency = observation.emergency > adsl::kEmergencyNone ? alptas::kUrgencyMayday
                                                                    : alptas::kUrgencyNormal;
    position.turn_rate_cdps = observation.turn_valid ? observation.turn_cdps : 0;
    position.stealth = observation.stealth;
    position.no_track = observation.no_track;
    // INFO: fc 09mar26 codes 0x12, 0x0A and 11, the constants SoftRF latest_encode sends
    position.horizontal_accuracy_dm = 32;
    position.vertical_accuracy_qm = 20;
    position.velocity_accuracy_dmps = 14;
    alptas::encode(frame, position, utc_s);
    return Status::Ok;
}

}  // namespace skyblip::simulator

#endif
