#include "core/comms/peer.h"

#include <cstdint>

#include "core/flight/state.h"
#include "core/settings/address.h"
#include "core/units/units.h"
#include "core/util/cbor.h"
#include "protocols/adsl/packet.h"

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

// INFO: fc 11nov26 a value with no age, or an age with no value, is half a field and refused
Status read_claim_position(const cbor::Reader& reader, PeerClaim& claim) {
    bool longitude = false, position_age = false, height_age = false;
    if (!take(reader.read_integer(link::peer_claim_key::kLatitude1e7), claim.latitude_1e7,
              claim.fix_valid) ||
        !take(reader.read_integer(link::peer_claim_key::kLongitude1e7), claim.longitude_1e7,
              longitude) ||
        !take(reader.read_unsigned(link::peer_claim_key::kPositionAgeMs), claim.fix_age_ms,
              position_age) ||
        !take(reader.read_integer(link::peer_claim_key::kAltitudeHaeMm), claim.altitude_hae_mm,
              claim.altitude_hae_valid) ||
        !take(reader.read_unsigned(link::peer_claim_key::kAltitudeHaeAgeMs),
              claim.altitude_hae_age_ms, height_age))
        return Status::Invalid;
    if (claim.fix_valid != longitude || claim.fix_valid != position_age ||
        claim.altitude_hae_valid != height_age)
        return Status::Invalid;
    if (!within(claim.latitude_1e7, -kLatitudeCeiling1e7, kLatitudeCeiling1e7) ||
        !within(claim.longitude_1e7, -kLongitudeCeiling1e7, kLongitudeCeiling1e7))
        return Status::OutOfRange;
    return Status::Ok;
}

Status read_claim_instant(const cbor::Reader& reader, PeerClaim& claim) {
    bool into = false;
    uint32_t into_ms = 0;
    if (!take(reader.read_unsigned(link::peer_claim_key::kAtS), claim.at_s, claim.utc_valid) ||
        !take(reader.read_unsigned(link::peer_claim_key::kIntoMs), into_ms, into))
        return Status::Invalid;
    if (into && !claim.utc_valid) return Status::Invalid;
    if (into_ms > kIntoCeilingMs) return Status::OutOfRange;
    claim.phase_valid = into;
    claim.into_ms = static_cast<uint16_t>(into_ms);
    return Status::Ok;
}

Status read_claim_pressure_and_icao(const cbor::Reader& reader, PeerClaim& claim) {
    bool age = false, icao = false;
    if (!take(reader.read_integer(link::peer_claim_key::kPressureAltitudeMm),
              claim.pressure_altitude_mm, claim.pressure_altitude_valid) ||
        !take(reader.read_unsigned(link::peer_claim_key::kPressureAltitudeAgeMs),
              claim.pressure_altitude_age_ms, age) ||
        !take(reader.read_unsigned(link::peer_claim_key::kOwnIcao), claim.own_icao, icao))
        return Status::Invalid;
    if (claim.pressure_altitude_valid != age) return Status::Invalid;
    return claim.own_icao > settings::kAddressMask ? Status::OutOfRange : Status::Ok;
}

// INFO: fc 13nov26 a callsign or a category with no address is half a field, refused whole
bool read_own_transponder(const cbor::Reader& reader, PeerHealth& health) {
    OwnTransponderHeard heard{};
    bool addressed = false, categorised = false;
    uint32_t category = 0;
    if (!take(reader.read_unsigned(link::health_key::kOwnTransponder), heard.address, addressed) ||
        !take(reader.read_unsigned(link::health_key::kOwnTransponderCategory), category,
              categorised))
        return false;
    const Status text = reader.read_text(link::health_key::kOwnTransponderCallsign, heard.callsign,
                                         kPeerCallsignBytes);
    if (text != Status::Ok && text != Status::NotFound) return false;
    const bool named = text == Status::Ok;
    if (!addressed) return !named && !categorised;
    if (heard.address == 0 || heard.address > settings::kAddressMask || category > UINT8_MAX)
        return false;
    heard.aircraft_category = static_cast<uint8_t>(category);
    health.own_transponder = heard;
    return true;
}

// INFO: fc 15nov26 a Bravo from before these keys sends none of them, which reads as no reading
bool read_bravo_status(const cbor::Reader& reader, PeerHealth& health) {
    uint32_t percent = 0, level = 0, rate = 0;
    bool charged = false, levelled = false, rated = false, percented = false;
    if (!take(reader.read_unsigned(link::health_key::kBatteryPercent), percent, percented) ||
        !take(reader.read_boolean(link::health_key::kCharging), health.charging, charged) ||
        !take(reader.read_unsigned(link::health_key::kPowerLevel), level, levelled) ||
        !take(reader.read_unsigned(link::health_key::kFrameRateHz), rate, rated))
        return false;
    if (percent > 100 || level > static_cast<uint32_t>(power::PowerLevel::Low)) return false;
    const Status text =
        reader.read_text(link::health_key::kFirmware, health.firmware, kPeerFirmwareBytes);
    if (text != Status::Ok && text != Status::NotFound) return false;
    if (percented) health.battery_percent = static_cast<uint8_t>(percent);
    health.power_level = static_cast<power::PowerLevel>(level);
    if (rated) health.frame_rate_hz = rate;
    return true;
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
    }
    if (claim.altitude_hae_valid) {
        writer.add_integer(link::peer_claim_key::kAltitudeHaeMm, claim.altitude_hae_mm);
        writer.add_unsigned(link::peer_claim_key::kAltitudeHaeAgeMs, claim.altitude_hae_age_ms);
    }
    if (claim.pressure_altitude_valid) {
        writer.add_integer(link::peer_claim_key::kPressureAltitudeMm, claim.pressure_altitude_mm);
        writer.add_unsigned(link::peer_claim_key::kPressureAltitudeAgeMs,
                            claim.pressure_altitude_age_ms);
    }
    if (claim.own_icao != 0) writer.add_unsigned(link::peer_claim_key::kOwnIcao, claim.own_icao);
    writer.add_boolean(link::peer_claim_key::kAlarmRequested, claim.alarm_requested);
    writer.add_boolean(link::peer_claim_key::kPaired, claim.paired);
    return writer.finish();
}

Result<PeerClaim> read_peer_claim(ConstByteSpan frame) {
    const cbor::Reader reader(frame);
    if (!reader.valid()) return Status::Invalid;
    const Result<uint32_t> code = reader.read_unsigned(link::kCommandKey);
    if (!code.ok() || code.value() != static_cast<uint32_t>(link::Command::PeerClaim))
        return Status::Invalid;
    const Result<uint32_t> epoch = reader.read_unsigned(link::peer_claim_key::kBootEpoch);
    const Result<bool> requested = reader.read_boolean(link::peer_claim_key::kAlarmRequested);
    const Result<bool> paired = reader.read_boolean(link::peer_claim_key::kPaired);
    if (!epoch.ok() || !requested.ok() || !paired.ok()) return Status::Invalid;
    PeerClaim claim{};
    claim.boot_epoch = epoch.value();
    claim.alarm_requested = requested.value();
    claim.paired = paired.value();
    Status status = read_claim_instant(reader, claim);
    if (status == Status::Ok) status = read_claim_position(reader, claim);
    if (status == Status::Ok) status = read_claim_pressure_and_icao(reader, claim);
    if (status != Status::Ok) return status;
    return claim;
}

int write_health(const PeerHealth& health, uint8_t* out, int capacity) {
    cbor::Writer writer(out, capacity);
    writer.add_code(link::kCommandKey, link::Command::Health);
    writer.add_unsigned(link::health_key::kProtocolVersion, kPeerProtocolVersion);
    writer.add_unsigned(link::health_key::kBootEpoch, health.boot_epoch);
    writer.add_boolean(link::health_key::kListening, health.listening);
    writer.add_boolean(link::health_key::kUtcValid, health.utc_valid);
    if (health.last_sequence_valid)
        writer.add_unsigned(link::health_key::kLastSequence, health.last_sequence);
    writer.add_boolean(link::health_key::kOwnIcaoSet, health.own_icao_set);
    writer.add_boolean(link::health_key::kAlarmGranted, health.alarm_granted);
    writer.add_code(link::health_key::kExportReference, health.export_reference);
    writer.add_boolean(link::health_key::kPaired, health.paired);
    if (health.own_transponder) {
        const OwnTransponderHeard& heard = *health.own_transponder;
        writer.add_unsigned(link::health_key::kOwnTransponder, heard.address);
        if (heard.callsign[0] != 0)
            writer.add_text(link::health_key::kOwnTransponderCallsign, heard.callsign);
        if (heard.aircraft_category != 0)
            writer.add_unsigned(link::health_key::kOwnTransponderCategory, heard.aircraft_category);
    }
    if (health.firmware[0] != 0) writer.add_text(link::health_key::kFirmware, health.firmware);
    if (health.battery_percent)
        writer.add_unsigned(link::health_key::kBatteryPercent, *health.battery_percent);
    writer.add_boolean(link::health_key::kCharging, health.charging);
    writer.add_code(link::health_key::kPowerLevel, health.power_level);
    if (health.frame_rate_hz)
        writer.add_unsigned(link::health_key::kFrameRateHz, *health.frame_rate_hz);
    return writer.finish();
}

int write_observation(const model::AircraftObservation& observation, const ObservationFrame& frame,
                      uint8_t* out, int capacity) {
    namespace key = link::observation_key;
    cbor::Writer writer(out, capacity);
    writer.add_code(link::kCommandKey, link::Command::Observation);
    writer.add_unsigned(key::kAddress, observation.address & settings::kAddressMask);
    writer.add_unsigned(key::kBootEpoch, frame.boot_epoch);
    writer.add_unsigned(key::kSequence, frame.sequence);
    writer.add_unsigned(key::kAtS, observation.received.at_s);
    if (observation.received.phase_valid)
        writer.add_unsigned(key::kIntoMs, observation.received.into_ms);
    if (observation.position_valid) {
        writer.add_integer(key::kLatitude1e7, observation.latitude_1e7);
        writer.add_integer(key::kLongitude1e7, observation.longitude_1e7);
    }
    if (observation.altitude_valid) {
        writer.add_integer(key::kAltitudeM, observation.altitude_m);
        writer.add_integer(key::kAltitudeAgeMs, observation.altitude_age_ms);
    }
    if (observation.pressure_altitude_valid) {
        writer.add_integer(key::kPressureAltitudeFt, observation.pressure_altitude_ft);
        writer.add_integer(key::kPressureAltitudeAgeMs, observation.pressure_altitude_age_ms);
    }
    if (observation.speed_valid) {
        writer.add_unsigned(key::kSpeedQ, observation.speed_q);
        writer.add_unsigned(key::kTrackC9, observation.track_c9);
        writer.add_integer(key::kSpeedAgeMs, observation.speed_age_ms);
    }
    if (observation.climb_valid) {
        writer.add_integer(key::kClimbE8, observation.climb_e8);
        writer.add_code(key::kClimbSource, observation.climb_source);
        writer.add_integer(key::kClimbAgeMs, observation.climb_age_ms);
    }
    if (observation.flight_state != 0) {
        writer.add_unsigned(key::kFlightState, observation.flight_state);
        writer.add_integer(key::kFlightStateAgeMs, observation.flight_state_age_ms);
    }
    if (observation.aircraft_category != 0)
        writer.add_unsigned(key::kAircraftCategory, observation.aircraft_category);
    if (observation.adsb_version_valid)
        writer.add_unsigned(key::kAdsbVersion, observation.adsb_version);
    if (observation.gva_valid) writer.add_unsigned(key::kGva, observation.gva);
    if (frame.callsign != nullptr && frame.callsign[0] != 0) {
        writer.add_text(key::kCallsign, frame.callsign);
        if (frame.callsign_heard_valid)
            writer.add_unsigned(key::kCallsignHeardS, frame.callsign_heard_s);
    }
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
    const Result<bool> paired = reader.read_boolean(link::health_key::kPaired);
    if (!epoch.ok() || !listening.ok() || !utc_valid.ok() || !own_icao_set.ok() || !paired.ok())
        return Status::Invalid;
    PeerHealth health{};
    health.boot_epoch = epoch.value();
    health.listening = listening.value();
    health.utc_valid = utc_valid.value();
    health.own_icao_set = own_icao_set.value();
    health.paired = paired.value();
    if (!read_own_transponder(reader, health) || !read_bravo_status(reader, health))
        return Status::Invalid;
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
    observation.address_mapping_table = adsl::kAddressTableIcao;
    observation.source = model::Source::Adsb;
    observation.rssi_valid = false;
    if (!read_position(reader, observation) || !read_altitudes(reader, observation) ||
        !read_velocity(reader, observation) || !read_identity(reader, observation) ||
        !read_callsign(reader, out))
        return Status::Invalid;
    return out;
}

}  // namespace skyblip::comms
