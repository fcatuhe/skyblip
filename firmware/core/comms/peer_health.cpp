#include "core/comms/peer_health.h"

#include <cstdint>

#include "core/comms/link_registry.h"

namespace skyblip::comms {

namespace {

constexpr int32_t kLatitudeCeiling1e7 = 900000000;
constexpr int32_t kLongitudeCeiling1e7 = 1800000000;
constexpr uint32_t kTrackCeilingC9 = 511;

template <class T>
bool take(Result<T> read, T& out, bool& present) {
    present = read.ok();
    if (present) out = read.value();
    return present || read.status() == Status::NotFound;
}

bool within(int32_t value, int32_t ceiling) { return value >= -ceiling && value <= ceiling; }

}  // namespace

void write_bravo_position(cbor::Writer& writer, const BravoPosition& position) {
    writer.add_integer(link::health_key::kLatitude1e7, position.latitude_1e7);
    writer.add_integer(link::health_key::kLongitude1e7, position.longitude_1e7);
    writer.add_unsigned(link::health_key::kPositionAgeMs, position.age_ms);
    if (position.altitude_m) writer.add_integer(link::health_key::kAltitudeM, *position.altitude_m);
    if (position.speed_q) {
        writer.add_unsigned(link::health_key::kSpeedQ, *position.speed_q);
        writer.add_unsigned(link::health_key::kTrackC9, position.track_c9);
    }
}

bool read_utc_source(const cbor::Reader& reader, PeerHealth& health) {
    uint32_t source = 0;
    bool sourced = false;
    if (!take(reader.read_unsigned(link::health_key::kUtcSource), source, sourced)) return false;
    if (!sourced) return true;
    if (!health.utc_valid || source > static_cast<uint32_t>(UtcSource::Go)) return false;
    health.utc_source = static_cast<UtcSource>(source);
    return true;
}

bool read_bravo_position(const cbor::Reader& reader, PeerHealth& health) {
    BravoPosition position{};
    int32_t altitude_m = 0;
    uint32_t age_ms = 0, speed_q = 0, track_c9 = 0;
    bool latitude = false, longitude = false, aged = false, high = false, fast = false,
         tracked = false;
    if (!take(reader.read_integer(link::health_key::kLatitude1e7), position.latitude_1e7,
              latitude) ||
        !take(reader.read_integer(link::health_key::kLongitude1e7), position.longitude_1e7,
              longitude) ||
        !take(reader.read_unsigned(link::health_key::kPositionAgeMs), age_ms, aged) ||
        !take(reader.read_integer(link::health_key::kAltitudeM), altitude_m, high) ||
        !take(reader.read_unsigned(link::health_key::kSpeedQ), speed_q, fast) ||
        !take(reader.read_unsigned(link::health_key::kTrackC9), track_c9, tracked))
        return false;
    if (!latitude) return !longitude && !aged && !high && !fast && !tracked;
    if (!longitude || !aged || fast != tracked || age_ms > UINT16_MAX ||
        !within(position.latitude_1e7, kLatitudeCeiling1e7) ||
        !within(position.longitude_1e7, kLongitudeCeiling1e7) ||
        (high && (altitude_m < INT16_MIN || altitude_m > INT16_MAX)) ||
        (fast && (speed_q > UINT16_MAX || track_c9 > kTrackCeilingC9)))
        return false;
    position.age_ms = static_cast<uint16_t>(age_ms);
    if (high) position.altitude_m = static_cast<int16_t>(altitude_m);
    if (fast) position.speed_q = static_cast<uint16_t>(speed_q);
    position.track_c9 = static_cast<uint16_t>(track_c9);
    health.position = position;
    return true;
}

}  // namespace skyblip::comms
