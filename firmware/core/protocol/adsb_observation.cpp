#include "core/protocol/adsb_observation.h"

#include "core/flight/state.h"
#include "core/protocol/adsb_category.h"
#include "core/settings/address.h"
#include "core/units/units.h"
#include "core/util/intmath.h"

namespace skyblip::protocol {

namespace {

constexpr uint8_t kCordic9PerCordic7 = 4;
constexpr int64_t kSecondsPerHour = 3600;

uint8_t flight_state_of(adsb::AirGround air_ground) {
    switch (air_ground) {
        case adsb::AirGround::Ground: return static_cast<uint8_t>(flight::FlightState::Ground);
        case adsb::AirGround::Airborne: return static_cast<uint8_t>(flight::FlightState::Airborne);
        case adsb::AirGround::Unknown: break;
    }
    return static_cast<uint8_t>(flight::FlightState::Unknown);
}

void speed_of(const adsb::Motion& motion, model::AircraftObservation& out) {
    if (motion.source == adsb::MotionSource::Velocity) {
        const int32_t east_mm_s = to_mm_s(Knots(motion.east_kt)).value;
        const int32_t north_mm_s = to_mm_s(Knots(motion.north_kt)).value;
        const uint16_t track_angle16 = static_cast<uint16_t>(arctangent(east_mm_s, north_mm_s));
        out.speed_q = to_speed_q(MillimetresPerSecond(
                                     static_cast<int32_t>(hypotenuse(east_mm_s, north_mm_s))))
                          .value;
        out.track_c9 = to_cordic9(centi_degrees_of_angle16(track_angle16)).value;
        return;
    }
    const int32_t speed_mm_s = static_cast<int32_t>(
        divide_rounded<int64_t>(int64_t{motion.speed_mkt} * kMetresPerNm, kSecondsPerHour));
    out.speed_q = to_speed_q(MillimetresPerSecond(speed_mm_s)).value;
    out.track_c9 =
        motion.track_valid ? static_cast<uint16_t>(motion.track_c7 * kCordic9PerCordic7) : 0;
}

}

void observation_of(const adsb::Track& track, model::AircraftObservation& out) {
    out = model::AircraftObservation{};
    out.address = track.address;
    out.address_mapping_table = settings::kIcaoAddressMappingTable;
    out.source = model::Source::Adsb;
    out.aircraft_category = track.identified ? adsb_emitter_to_adsl_category(
                                                   track.emitter_type_code, track.emitter_category)
                                             : 0;
    out.received =
        events::Stamp{track.received.utc_s, track.received.utc_ms, track.received.utc_valid};
    out.rssi_dbm = track.rssi_dbm;
    out.rssi_valid = track.rssi_valid;
    out.at_ms = track.at_ms;
    out.position_valid = track.position_valid;
    if (track.position_valid) {
        out.latitude_1e7 = track.position.latitude_1e7;
        out.longitude_1e7 = track.position.longitude_1e7;
    }
    if (track.air_ground != adsb::AirGround::Unknown) {
        out.flight_state = flight_state_of(track.air_ground);
        out.flight_state_age_ms = track.air_ground_age_ms;
    }
    out.speed_valid = track.motion.source != adsb::MotionSource::None;
    if (out.speed_valid) {
        speed_of(track.motion, out);
        out.speed_age_ms = track.motion_age_ms;
    }
    out.climb_valid = track.climb_valid;
    if (track.climb_valid) {
        out.climb_e8 = to_climb_e8(to_mm_s(FeetPerMinute(track.climb_fpm))).value;
        out.climb_source = track.climb_source == adsb::HeightSource::Gnss
                               ? model::ClimbSource::Gnss
                               : model::ClimbSource::Barometric;
        out.climb_age_ms = track.climb_age_ms;
    }
    out.pressure_altitude_valid = track.pressure_altitude_valid;
    if (track.pressure_altitude_valid) {
        out.pressure_altitude_ft = track.pressure_altitude_ft;
        out.pressure_altitude_age_ms = track.pressure_altitude_age_ms;
    }
    out.height_difference_valid = track.height_difference_valid;
    if (track.height_difference_valid) {
        out.height_difference_ft = static_cast<int16_t>(track.height_difference_ft);
        out.height_difference_age_ms = track.height_difference_age_ms;
    }
    out.altitude_valid = track.gnss_height_valid;
    if (track.gnss_height_valid) {
        out.altitude_m = to_metres(Feet(track.gnss_height_ft)).value;
        out.altitude_age_ms = track.gnss_height_age_ms;
    }
    out.adsb_version = track.version;
    out.adsb_version_valid = track.version_valid;
    out.gva = track.gva;
    out.gva_valid = track.gva_valid;
}

bool adsb_fold(adsb::Assembly& assembly, const adsb::Message& message, uint32_t at_ms,
               const events::Stamp& received, const adsb::Reference& reference,
               model::AircraftObservation& out) {
    adsb::Track track;
    const adsb::Receipt receipt{received.at_s, received.into_ms, received.phase_valid};
    if (!assembly.fold(message, at_ms, receipt, reference, track)) return false;
    observation_of(track, out);
    return true;
}

}
