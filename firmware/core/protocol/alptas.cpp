#include "core/protocol/alptas.h"

#include "core/flight/state.h"
#include "core/model/aircraft.h"
#include "core/protocol/nmea_out.h"
#include "core/traffic/sanity.h"
#include "protocols/adsl/packet.h"
#include "protocols/alptas/frame.h"
#include "protocols/common/geometry.h"

namespace skyblip::protocol {

uint8_t alptas_type_to_adsl_category(uint8_t alptas_type) {
    static const uint8_t kCategoryOfType[20] = {0,  4, 1, 3,  8, 1, 16, 7,  1,  2,
                                                10, 5, 5, 11, 0, 0, 9,  11, 12, 13};
    return alptas_type < 20 ? kCategoryOfType[alptas_type] : 0;
}

uint8_t alptas_flight_state(uint8_t alptas_airborne) {
    switch (alptas_airborne) {
        case alptas::kAirborneOnGround: return static_cast<uint8_t>(flight::FlightState::Ground);
        case alptas::kAirborneFlying:
        case alptas::kAirborneCircling: return static_cast<uint8_t>(flight::FlightState::Airborne);
        default: return static_cast<uint8_t>(flight::FlightState::Unknown);
    }
}

// INFO: fc 17nov26 G.1.4 has no urgency short of distress, so Pan-Pan shows as an emergency too
uint8_t alptas_urgency_to_emergency(uint8_t alptas_urgency) {
    switch (alptas_urgency) {
        case alptas::kUrgencyPanPan:
        case alptas::kUrgencyMayday: return adsl::kEmergencyGeneral;
        default: return adsl::kEmergencyNone;
    }
}

// INFO: fc 17nov26 the fix field counts quarter seconds modulo 16 s, and a frame goes out by 1.2 s
int32_t alptas_fix_age_ms(uint8_t fix_quarters, const events::Stamp& heard) {
    if (!heard.phase_valid) return 0;
    const uint32_t heard_quarter = heard.at_s * 4u + heard.into_ms / 250u;
    const uint32_t quarters_ago = (heard_quarter - fix_quarters) & 0x3Fu;
    const int32_t age_ms = static_cast<int32_t>(quarters_ago * 250u + heard.into_ms % 250u);
    return age_ms <= kAlptasFixAgeCeilingMs ? age_ms : 0;
}

uint8_t alptas_address_type_to_address_mapping_table(uint8_t alptas_address_type) {
    switch (alptas_address_type & 3) {
        case alptas::kAddressTypeIcao: return adsl::kAddressTableIcao;
        case alptas::kAddressTypeManufacturer: return adsl::kAddressTableFlarm;
        default: return 0;
    }
}

uint8_t address_mapping_table_to_alptas_address_type(uint8_t address_mapping_table) {
    switch (address_mapping_table) {
        case adsl::kAddressTableIcao: return alptas::kAddressTypeIcao;
        case adsl::kAddressTableFlarm: return alptas::kAddressTypeManufacturer;
        default: return 0;
    }
}

namespace {

void to_observation(const alptas::Position& position, uint32_t rx_utc_s, uint32_t address,
                    model::AircraftObservation& out) {
    out = model::AircraftObservation{};
    out.address = address;
    out.address_mapping_table = alptas_address_type_to_address_mapping_table(position.address_type);
    out.aircraft_category = alptas_type_to_adsl_category(position.aircraft_type);
    out.flight_state = alptas_flight_state(position.airborne);
    out.emergency = alptas_urgency_to_emergency(position.urgency);
    out.turn_cdps = static_cast<int16_t>(position.turn_rate_cdps);
    out.turn_valid = true;
    out.stealth = position.stealth;
    out.no_track = position.no_track;
    out.latitude_1e7 = position.latitude_1e7;
    out.longitude_1e7 = position.longitude_1e7;
    out.altitude_m = position.altitude_m;
    out.speed_q = static_cast<uint16_t>(wire::divide_rounded<int32_t>(position.speed_e1 * 2, 5));
    out.climb_e8 = static_cast<int16_t>(wire::divide_rounded<int32_t>(position.climb_e1 * 4, 5));
    out.track_c9 = static_cast<uint16_t>(
        wire::divide_rounded<int32_t>(static_cast<int32_t>(position.track_deg) * 512, 360) & 0x1FF);
    out.received.at_s = rx_utc_s;
    out.source = model::Source::Alptas;
    out.speed_valid = true;
    out.climb_valid = true;
    out.position_valid = true;
}

// INFO: fc 17nov26 a wrong key reads as rated, and lands within radio range once in 130
bool heard_from_range(const alptas::Position& position, int32_t reference_latitude_1e7,
                      int32_t reference_longitude_1e7) {
    if (position.sil == 0 && position.sda == 0 && position.nic == 0) return true;
    const wire::NorthEast offset =
        wire::north_east_m(reference_latitude_1e7, reference_longitude_1e7, position.latitude_1e7,
                           position.longitude_1e7);
    return wire::hypotenuse(offset.north_m, offset.east_m) <=
           static_cast<uint32_t>(traffic::plausible_range_m(model::Source::Alptas));
}

}

Status alptas_decode(const uint8_t* frame, uint32_t rx_utc_s, int32_t reference_latitude_1e7,
                     int32_t reference_longitude_1e7, model::AircraftObservation& out) {
    alptas::Position position;
    const wire::Status decoded =
        alptas::decode(frame, rx_utc_s, reference_latitude_1e7, reference_longitude_1e7, position);
    if (decoded != wire::Status::Ok) return status_of(decoded);
    if (!heard_from_range(position, reference_latitude_1e7, reference_longitude_1e7))
        return Status::Invalid;
    to_observation(position, rx_utc_s, position.address & 0xFFFFFFu, out);
    return Status::Ok;
}

Status AlptasReceiver::decode(const uint8_t* frame, uint32_t rx_utc_s,
                              int32_t reference_latitude_1e7, int32_t reference_longitude_1e7,
                              const events::Stamp& heard, model::AircraftObservation& out) {
    alptas::Position position;
    const wire::Status decoded =
        alptas::decode(frame, rx_utc_s, reference_latitude_1e7, reference_longitude_1e7, position);
    if (decoded != wire::Status::Ok) return status_of(decoded);
    if (!heard_from_range(position, reference_latitude_1e7, reference_longitude_1e7))
        return Status::Invalid;
    const uint32_t address = position.address_type == alptas::kAddressTypeRandom
                                 ? shown_address(position.address, position.random, rx_utc_s)
                                 : position.address & 0xFFFFFFu;
    to_observation(position, rx_utc_s, address, out);
    out.fix_age_ms = alptas_fix_age_ms(position.fix_quarters, heard);
    return Status::Ok;
}

uint32_t AlptasReceiver::shown_address(uint32_t address, uint8_t random, uint32_t heard_s) {
    Followed* slot = nullptr;
    for (Followed& followed : followed_) {
        if (!followed.used) continue;
        if (heard_s - followed.heard_s > kFollowS) {
            followed.used = false;
            continue;
        }
        if (followed.current == address || followed.next == address) slot = &followed;
    }
    if (slot == nullptr) {
        slot = followed_.data();
        for (Followed& followed : followed_) {
            if (!followed.used) {
                slot = &followed;
                break;
            }
            if (followed.heard_s < slot->heard_s) slot = &followed;
        }
        *slot = Followed{};
        slot->used = true;
        slot->shown = address & 0xFFFFFFu;
    }
    slot->current = address;
    slot->next = alptas::random_address_after(address, random);
    slot->heard_s = heard_s;
    return slot->shown;
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

}
