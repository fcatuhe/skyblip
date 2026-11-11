#include "core/comms/peer.h"

#include "core/util/cbor.h"

namespace skyblip::comms {

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
    const Result<bool> own_icao_set = reader.read_boolean(link::health_key::kOwnIcaoSet);
    if (!epoch.ok() || !own_icao_set.ok()) return Status::Invalid;
    PeerHealth health{};
    health.boot_epoch = epoch.value();
    health.own_icao_set = own_icao_set.value();
    return health;
}

}  // namespace skyblip::comms
