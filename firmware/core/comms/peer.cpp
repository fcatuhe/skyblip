#include "core/comms/peer.h"

#include <cstdint>

#include "core/flight/state.h"
#include "core/settings/address.h"
#include "core/units/units.h"
#include "core/util/cbor.h"

namespace skyblip::comms {

namespace {

// INFO: fc 12nov26 schemas/link.v1.json, observation: 0 to 999 milliseconds into at_s
constexpr uint32_t kIntoCeilingMs = 999;
constexpr uint32_t kTrackCeilingC9 = 511;
constexpr uint32_t kAdsbVersionCeiling = 7;
constexpr uint32_t kGvaCeiling = 3;
constexpr int32_t kLatitudeCeiling1e7 = 900000000;
constexpr int32_t kLongitudeCeiling1e7 = 1800000000;
// INFO: fc 12nov26 the extremes protocol::altitude_ft_of decodes, Gillham's lowest and highest code
constexpr int32_t kPressureAltitudeFloorFt = -1200;
constexpr int32_t kPressureAltitudeCeilingFt = 126700;
// INFO: fc 12nov26 the widest unsaturated GNSS-baro difference ADS-B carries, 125 steps of 25 ft
constexpr int32_t kHeightDifferenceCeilingFt = 3125;
constexpr int32_t kAltitudeFloorM =
    to_metres(Feet(kPressureAltitudeFloorFt - kHeightDifferenceCeilingFt)).value;
constexpr int32_t kAltitudeCeilingM =
    to_metres(Feet(kPressureAltitudeCeilingFt + kHeightDifferenceCeilingFt)).value;

bool within(int32_t value, int32_t floor, int32_t ceiling) {
    return value >= floor && value <= ceiling;
}

// INFO: fc 12nov26 a key sent wrong refuses the frame, so a reader never takes half of it
template <class T>
bool take(Result<T> read, T& out, bool& present) {
    present = read.ok();
    if (present) out = read.value();
    return present || read.status() == Status::NotFound;
}

bool take_age(const cbor::Reader& reader, uint32_t key, bool present, int32_t& out) {
    bool aged = false;
    if (!take(reader.read_integer(key), out, aged)) return false;
    return aged == present;
}

bool fits_int16(int32_t value) { return value >= INT16_MIN && value <= INT16_MAX; }

bool read_position(const cbor::Reader& reader, model::AircraftObservation& out) {
    bool latitude = false, longitude = false;
    if (!take(reader.read_integer(link::observation_key::kLatitude1e7), out.latitude_1e7,
              latitude) ||
        !take(reader.read_integer(link::observation_key::kLongitude1e7), out.longitude_1e7,
              longitude))
        return false;
    out.position_valid = latitude && longitude;
    if (out.position_valid &&
        (!within(out.latitude_1e7, -kLatitudeCeiling1e7, kLatitudeCeiling1e7) ||
         !within(out.longitude_1e7, -kLongitudeCeiling1e7, kLongitudeCeiling1e7)))
        return false;
    return latitude == longitude;
}

bool read_altitudes(const cbor::Reader& reader, model::AircraftObservation& out) {
    if (!take(reader.read_integer(link::observation_key::kAltitudeM), out.altitude_m,
              out.altitude_valid) ||
        !take_age(reader, link::observation_key::kAltitudeAgeMs, out.altitude_valid,
                  out.altitude_age_ms) ||
        (out.altitude_valid && !within(out.altitude_m, kAltitudeFloorM, kAltitudeCeilingM)))
        return false;
    return take(reader.read_integer(link::observation_key::kPressureAltitudeFt),
                out.pressure_altitude_ft, out.pressure_altitude_valid) &&
           take_age(reader, link::observation_key::kPressureAltitudeAgeMs,
                    out.pressure_altitude_valid, out.pressure_altitude_age_ms) &&
           (!out.pressure_altitude_valid ||
            within(out.pressure_altitude_ft, kPressureAltitudeFloorFt, kPressureAltitudeCeilingFt));
}

bool read_velocity(const cbor::Reader& reader, model::AircraftObservation& out) {
    uint32_t speed_q = 0, track_c9 = 0;
    bool track = false;
    if (!take(reader.read_unsigned(link::observation_key::kSpeedQ), speed_q, out.speed_valid) ||
        !take(reader.read_unsigned(link::observation_key::kTrackC9), track_c9, track) ||
        !take_age(reader, link::observation_key::kSpeedAgeMs, out.speed_valid, out.speed_age_ms))
        return false;
    if (track != out.speed_valid || speed_q > UINT16_MAX || track_c9 > kTrackCeilingC9)
        return false;
    out.speed_q = static_cast<uint16_t>(speed_q);
    out.track_c9 = static_cast<uint16_t>(track_c9);

    int32_t climb_e8 = 0;
    uint32_t climb_source = 0;
    bool sourced = false;
    if (!take(reader.read_integer(link::observation_key::kClimbE8), climb_e8, out.climb_valid) ||
        !take(reader.read_unsigned(link::observation_key::kClimbSource), climb_source, sourced) ||
        !take_age(reader, link::observation_key::kClimbAgeMs, out.climb_valid, out.climb_age_ms))
        return false;
    if (sourced != out.climb_valid || !fits_int16(climb_e8) ||
        climb_source > static_cast<uint32_t>(model::ClimbSource::Barometric))
        return false;
    out.climb_e8 = static_cast<int16_t>(climb_e8);
    out.climb_source = static_cast<model::ClimbSource>(climb_source);
    return true;
}

bool read_identity(const cbor::Reader& reader, model::AircraftObservation& out) {
    uint32_t flight_state = 0, category = 0, version = 0, gva = 0;
    bool stated = false, categorised = false;
    if (!take(reader.read_unsigned(link::observation_key::kFlightState), flight_state, stated) ||
        !take_age(reader, link::observation_key::kFlightStateAgeMs, stated,
                  out.flight_state_age_ms) ||
        !take(reader.read_unsigned(link::observation_key::kAircraftCategory), category,
              categorised) ||
        !take(reader.read_unsigned(link::observation_key::kAdsbVersion), version,
              out.adsb_version_valid) ||
        !take(reader.read_unsigned(link::observation_key::kGva), gva, out.gva_valid))
        return false;
    const bool known_state = flight_state == static_cast<uint32_t>(flight::FlightState::Ground) ||
                             flight_state == static_cast<uint32_t>(flight::FlightState::Airborne);
    if ((stated && !known_state) || category > UINT8_MAX || version > kAdsbVersionCeiling ||
        gva > kGvaCeiling)
        return false;
    out.flight_state = static_cast<uint8_t>(flight_state);
    out.aircraft_category = static_cast<uint8_t>(category);
    out.adsb_version = static_cast<uint8_t>(version);
    out.gva = static_cast<uint8_t>(gva);
    return true;
}

bool read_callsign(const cbor::Reader& reader, PeerObservation& out) {
    const Status text =
        reader.read_text(link::observation_key::kCallsign, out.callsign, kPeerCallsignBytes);
    if (text != Status::Ok && text != Status::NotFound) return false;
    bool dated = false;
    if (!take(reader.read_unsigned(link::observation_key::kCallsignHeardS), out.callsign_heard_s,
              dated))
        return false;
    out.callsign_dated = dated && text == Status::Ok;
    return !dated || text == Status::Ok;
}

}  // namespace

int write_peer_claim(const PeerClaim& claim, uint8_t* out, int capacity) {
    cbor::Writer writer(out, capacity);
    writer.add_code(link::kCommandKey, link::Command::PeerClaim);
    writer.add_unsigned(link::peer_claim_key::kBootEpoch, claim.boot_epoch);
    if (claim.utc_valid) {
        writer.add_unsigned(link::peer_claim_key::kAtS, claim.at_s);
        if (claim.phase_valid) writer.add_unsigned(link::peer_claim_key::kIntoMs, claim.into_ms);
    }
    if (claim.fix_valid) {
        writer.add_integer(link::peer_claim_key::kLatitude1e7, claim.latitude_1e7);
        writer.add_integer(link::peer_claim_key::kLongitude1e7, claim.longitude_1e7);
        writer.add_unsigned(link::peer_claim_key::kPositionAgeMs, claim.fix_age_ms);
        writer.add_integer(link::peer_claim_key::kAltitudeHaeMm, claim.altitude_hae_mm);
        writer.add_unsigned(link::peer_claim_key::kAltitudeHaeAgeMs, claim.fix_age_ms);
    }
    if (claim.pressure_altitude_valid) {
        writer.add_integer(link::peer_claim_key::kPressureAltitudeMm, claim.pressure_altitude_mm);
        writer.add_unsigned(link::peer_claim_key::kPressureAltitudeAgeMs,
                            claim.pressure_altitude_age_ms);
    }
    if (claim.own_icao != 0) writer.add_unsigned(link::peer_claim_key::kOwnIcao, claim.own_icao);
    writer.add_boolean(link::peer_claim_key::kAlarmRequested, false);
    writer.add_unsigned(link::peer_claim_key::kAlarmPasses, claim.alarm_passes);
    writer.add_unsigned(link::peer_claim_key::kAlarmVolume, claim.alarm_volume);
    return writer.finish();
}

Result<link::Command> peer_command(ConstByteSpan frame) {
    const cbor::Reader reader(frame);
    if (!reader.valid()) return Status::Invalid;
    const Result<uint32_t> code = reader.read_unsigned(link::kCommandKey);
    if (!code.ok()) return code.status();
    return static_cast<link::Command>(code.value());
}

Result<PeerHealth> read_health(ConstByteSpan frame) {
    const cbor::Reader reader(frame);
    const Result<uint32_t> code = reader.read_unsigned(link::kCommandKey);
    if (!code.ok() || code.value() != static_cast<uint32_t>(link::Command::Health))
        return Status::Invalid;
    const Result<uint32_t> version = reader.read_unsigned(link::health_key::kProtocolVersion);
    if (!version.ok()) return Status::Invalid;
    if (version.value() != kPeerProtocolVersion) return Status::Unsupported;
    const Result<uint32_t> epoch = reader.read_unsigned(link::health_key::kBootEpoch);
    const Result<bool> listening = reader.read_boolean(link::health_key::kListening);
    const Result<bool> utc_valid = reader.read_boolean(link::health_key::kUtcValid);
    const Result<bool> own_icao_set = reader.read_boolean(link::health_key::kOwnIcaoSet);
    if (!epoch.ok() || !listening.ok() || !utc_valid.ok() || !own_icao_set.ok())
        return Status::Invalid;
    PeerHealth health{};
    health.boot_epoch = epoch.value();
    health.listening = listening.value();
    health.utc_valid = utc_valid.value();
    health.own_icao_set = own_icao_set.value();
    return health;
}

// INFO: fc 12nov26 a key a newer Bravo adds is skipped: protocol_version moves on a meaning only
Result<PeerObservation> read_observation(ConstByteSpan frame) {
    const cbor::Reader reader(frame);
    const Result<uint32_t> code = reader.read_unsigned(link::kCommandKey);
    if (!code.ok() || code.value() != static_cast<uint32_t>(link::Command::Observation))
        return Status::Invalid;
    const Result<uint32_t> address = reader.read_unsigned(link::observation_key::kAddress);
    const Result<uint32_t> epoch = reader.read_unsigned(link::observation_key::kBootEpoch);
    const Result<uint32_t> sequence = reader.read_unsigned(link::observation_key::kSequence);
    const Result<uint32_t> at_s = reader.read_unsigned(link::observation_key::kAtS);
    if (!address.ok() || !epoch.ok() || !sequence.ok() || !at_s.ok() ||
        address.value() > settings::kAddressMask)
        return Status::Invalid;

    PeerObservation out{};
    out.boot_epoch = epoch.value();
    out.sequence = sequence.value();
    out.at.at_s = at_s.value();
    uint32_t into_ms = 0;
    if (!take(reader.read_unsigned(link::observation_key::kIntoMs), into_ms, out.at.phase_valid) ||
        into_ms > kIntoCeilingMs)
        return Status::Invalid;
    out.at.into_ms = static_cast<uint16_t>(into_ms);

    model::AircraftObservation& observation = out.observation;
    observation.address = address.value();
    observation.address_mapping_table = settings::kIcaoAddressMappingTable;
    observation.source = model::Source::Adsb;
    if (!read_position(reader, observation) || !read_altitudes(reader, observation) ||
        !read_velocity(reader, observation) || !read_identity(reader, observation) ||
        !read_callsign(reader, out))
        return Status::Invalid;
    return out;
}

}  // namespace skyblip::comms
