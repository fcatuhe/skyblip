#include "core/comms/peer.h"

#include "core/settings/address.h"
#include "core/util/cbor.h"

namespace skyblip::comms {

namespace {

constexpr int32_t kLatitudeCeiling1e7 = 900000000;
constexpr int32_t kLongitudeCeiling1e7 = 1800000000;
constexpr uint32_t kLastMillisecondOfSecond = 999;
constexpr uint32_t kAlarmVolumeCeiling = 2;

template <class Value>
Status optional_key(const Result<Value>& read, bool& present, Value& out) {
    present = read.ok();
    if (read.ok()) out = read.value();
    return read.status() == Status::NotFound ? Status::Ok : read.status();
}

// INFO: fc 11nov26 a value with no age, or an age with no value, is half a field and refused
Status both_or_neither(bool value, bool age) { return value == age ? Status::Ok : Status::Invalid; }

Status read_position(const cbor::Reader& reader, PeerClaim& claim) {
    bool longitude = false, position_age = false, height_age = false;
    Status status = optional_key(reader.read_integer(link::peer_claim_key::kLatitude1e7),
                                 claim.fix_valid, claim.latitude_1e7);
    if (status == Status::Ok)
        status = optional_key(reader.read_integer(link::peer_claim_key::kLongitude1e7), longitude,
                              claim.longitude_1e7);
    if (status == Status::Ok)
        status = optional_key(reader.read_unsigned(link::peer_claim_key::kPositionAgeMs),
                              position_age, claim.fix_age_ms);
    if (status == Status::Ok)
        status = optional_key(reader.read_integer(link::peer_claim_key::kAltitudeHaeMm),
                              claim.altitude_hae_valid, claim.altitude_hae_mm);
    if (status == Status::Ok)
        status = optional_key(reader.read_unsigned(link::peer_claim_key::kAltitudeHaeAgeMs),
                              height_age, claim.altitude_hae_age_ms);
    if (status != Status::Ok) return status;
    if (claim.fix_valid != longitude || claim.fix_valid != position_age ||
        claim.altitude_hae_valid != height_age)
        return Status::Invalid;
    if (claim.latitude_1e7 < -kLatitudeCeiling1e7 || claim.latitude_1e7 > kLatitudeCeiling1e7)
        return Status::OutOfRange;
    if (claim.longitude_1e7 < -kLongitudeCeiling1e7 || claim.longitude_1e7 > kLongitudeCeiling1e7)
        return Status::OutOfRange;
    return Status::Ok;
}

Status read_instant(const cbor::Reader& reader, PeerClaim& claim) {
    bool into = false;
    uint32_t into_ms = 0;
    Status status =
        optional_key(reader.read_unsigned(link::peer_claim_key::kAtS), claim.utc_valid, claim.at_s);
    if (status == Status::Ok)
        status = optional_key(reader.read_unsigned(link::peer_claim_key::kIntoMs), into, into_ms);
    if (status != Status::Ok) return status;
    if (into && !claim.utc_valid) return Status::Invalid;
    if (into_ms > kLastMillisecondOfSecond) return Status::OutOfRange;
    claim.phase_valid = into;
    claim.into_ms = static_cast<uint16_t>(into_ms);
    return Status::Ok;
}

Status read_pressure_and_icao(const cbor::Reader& reader, PeerClaim& claim) {
    bool age = false, icao = false;
    Status status = optional_key(reader.read_integer(link::peer_claim_key::kPressureAltitudeMm),
                                 claim.pressure_altitude_valid, claim.pressure_altitude_mm);
    if (status == Status::Ok)
        status = optional_key(reader.read_unsigned(link::peer_claim_key::kPressureAltitudeAgeMs),
                              age, claim.pressure_altitude_age_ms);
    if (status == Status::Ok) status = both_or_neither(claim.pressure_altitude_valid, age);
    if (status == Status::Ok)
        status = optional_key(reader.read_unsigned(link::peer_claim_key::kOwnIcao), icao,
                              claim.own_icao);
    if (status != Status::Ok) return status;
    return claim.own_icao > settings::kAddressMask ? Status::OutOfRange : Status::Ok;
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
    writer.add_boolean(link::peer_claim_key::kAlarmRequested, false);
    writer.add_unsigned(link::peer_claim_key::kAlarmPasses, claim.alarm_passes);
    writer.add_unsigned(link::peer_claim_key::kAlarmVolume, claim.alarm_volume);
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
    const Result<uint32_t> passes = reader.read_unsigned(link::peer_claim_key::kAlarmPasses);
    const Result<uint32_t> volume = reader.read_unsigned(link::peer_claim_key::kAlarmVolume);
    const Result<bool> requested = reader.read_boolean(link::peer_claim_key::kAlarmRequested);
    const Result<bool> paired = reader.read_boolean(link::peer_claim_key::kPaired);
    if (!epoch.ok() || !passes.ok() || !volume.ok() || !requested.ok() || !paired.ok())
        return Status::Invalid;
    if (volume.value() > kAlarmVolumeCeiling) return Status::OutOfRange;
    PeerClaim claim{};
    claim.boot_epoch = epoch.value();
    claim.alarm_passes = passes.value();
    claim.alarm_volume = static_cast<uint8_t>(volume.value());
    claim.paired = paired.value();
    Status status = read_instant(reader, claim);
    if (status == Status::Ok) status = read_position(reader, claim);
    if (status == Status::Ok) status = read_pressure_and_icao(reader, claim);
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
    // INFO: fc 11nov26 the grant is #179's lease, so Bravo keeps the alarm until it lands
    writer.add_boolean(link::health_key::kAlarmGranted, false);
    writer.add_code(link::health_key::kExportReference, health.export_reference);
    writer.add_boolean(link::health_key::kPaired, health.paired);
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
    const Result<bool> own_icao_set = reader.read_boolean(link::health_key::kOwnIcaoSet);
    const Result<bool> paired = reader.read_boolean(link::health_key::kPaired);
    if (!epoch.ok() || !own_icao_set.ok() || !paired.ok()) return Status::Invalid;
    PeerHealth health{};
    health.boot_epoch = epoch.value();
    health.own_icao_set = own_icao_set.value();
    health.paired = paired.value();
    return health;
}

}  // namespace skyblip::comms
