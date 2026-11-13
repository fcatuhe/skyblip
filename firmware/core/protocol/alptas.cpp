#include "core/protocol/alptas.h"

#include "core/flight/state.h"
#include "core/model/aircraft.h"
#include "core/protocol/nmea_out.h"
#include "core/util/intmath.h"
#include "protocols/alptas/frame.h"

namespace skyblip::protocol {

// INFO: fc 09nov26 FLARM ICD AcftType 0x0 to 0xF in order, where 0x0, 0xA and 0xE name no type
uint8_t alptas_type_to_adsl_category(uint8_t alptas_type) {
    static const uint8_t kCategoryOfType[16] = {0, 4, 1, 3, 8, 1, 16, 7, 1, 2, 0, 5, 5, 11, 0, 0};
    return alptas_type < 16 ? kCategoryOfType[alptas_type] : 0;
}

uint8_t alptas_flight_state(uint8_t alptas_airborne) {
    switch (alptas_airborne) {
        case alptas::kAirborneOnGround: return static_cast<uint8_t>(flight::FlightState::Ground);
        case alptas::kAirborneFlying:
        case alptas::kAirborneCircling: return static_cast<uint8_t>(flight::FlightState::Airborne);
        default: return static_cast<uint8_t>(flight::FlightState::Unknown);
    }
}

uint8_t alptas_address_type_to_address_mapping_table(uint8_t alptas_address_type) {
    switch (alptas_address_type & 3) {
        case alptas::kAddressTypeIcao: return 0x05;
        case alptas::kAddressTypeFlarm: return 0x06;
        default: return 0;
    }
}

uint8_t address_mapping_table_to_alptas_address_type(uint8_t address_mapping_table) {
    switch (address_mapping_table) {
        case 0x05: return alptas::kAddressTypeIcao;
        case 0x06: return alptas::kAddressTypeFlarm;
        default: return 0;
    }
}

Status alptas_decode(const uint8_t* frame, uint32_t rx_utc_s, int32_t reference_latitude_1e7,
                     int32_t reference_longitude_1e7, model::AircraftObservation& out) {
    alptas::Position position;
    const wire::Status decoded =
        alptas::decode(frame, rx_utc_s, reference_latitude_1e7, reference_longitude_1e7, position);
    if (decoded != wire::Status::Ok) return status_of(decoded);

    out = model::AircraftObservation{};
    out.address = position.address;
    out.address_mapping_table = alptas_address_type_to_address_mapping_table(position.address_type);
    out.aircraft_category = alptas_type_to_adsl_category(position.aircraft_type);
    out.flight_state = alptas_flight_state(position.airborne);
    out.emergency = 1;  // INFO: fc 09mar26 no emergency field on the wire; 1 is ADS-L "OK"
    out.latitude_1e7 = position.latitude_1e7;
    out.longitude_1e7 = position.longitude_1e7;
    out.altitude_m = position.altitude_m;
    out.speed_q = static_cast<uint16_t>(divide_rounded<int32_t>(position.speed_e1 * 2, 5));
    out.climb_e8 = static_cast<int16_t>(divide_rounded<int32_t>(position.climb_e1 * 4, 5));
    out.track_c9 = static_cast<uint16_t>(
        divide_rounded<int32_t>(static_cast<int32_t>(position.course_deg) * 512, 360) & 0x1FF);
    out.received.at_s = rx_utc_s;
    out.source = model::Source::Alptas;
    out.speed_valid = true;
    out.climb_valid = true;
    out.position_valid = true;
    return Status::Ok;
}

Status alptas_encode(uint8_t* frame, const model::AircraftObservation& observation,
                     uint32_t utc_s) {
    if (!observation.position_valid) return Status::Invalid;
    alptas::Position position;
    position.address = observation.address;
    position.address_type =
        address_mapping_table_to_alptas_address_type(observation.address_mapping_table);
    position.aircraft_type = adsl_category_to_alptas_type(observation.aircraft_category);
    position.airborne = flight::on_ground(observation.flight_state) ? alptas::kAirborneOnGround
                                                                    : alptas::kAirborneFlying;
    position.latitude_1e7 = observation.latitude_1e7;
    position.longitude_1e7 = observation.longitude_1e7;
    position.altitude_m = observation.altitude_m;
    position.speed_e1 =
        observation.speed_valid ? divide_rounded<int32_t>(observation.speed_q * 5, 2) : 0;
    position.climb_e1 =
        observation.climb_valid ? divide_rounded<int32_t>(observation.climb_e8 * 5, 4) : 0;
    position.course_deg = static_cast<uint16_t>(
        static_cast<uint32_t>(divide_rounded<int32_t>(observation.track_c9 * 360, 512)) % 360u);
    alptas::encode(frame, position, utc_s);
    return Status::Ok;
}

}
