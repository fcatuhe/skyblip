#include "core/flight/log_record.h"

#include <cstring>

#include "core/model/ownship.h"
#include "core/settings/address.h"
#include "core/store/sector.h"
#include "core/units/units.h"
#include "core/util/clamp.h"
#include "protocols/common/crc.h"
#include "protocols/common/little_endian.h"

namespace skyblip::flight {

namespace {

constexpr uint32_t kTimeOffsetCeilingS = 0xFFFF;
constexpr uint16_t kHdopTenthsCeiling = 255;
constexpr uint16_t kTrackMask = 0x01FF;
constexpr uint32_t kChecksummedBytes = kLogRecordBytes - 2;
constexpr uint32_t kFixBodyOffset = 2;
static_assert(kFixBodyOffset + kLogFixBodyBytes == kChecksummedBytes);

constexpr uint8_t kStatusFlagBatteryValid = 1u << 0;
constexpr uint8_t kStatusFlagCharging = 1u << 1;
constexpr uint8_t kStatusFlagExternalPower = 1u << 2;
constexpr uint8_t kTargetFlagDismissed = 1u << 0;
constexpr uint8_t kTargetFlagInFormation = 1u << 1;
// INFO: fc 11nov26 targets before bit 2 wrote 0 for an unknown altitude, so bit 2 vouches for bit 3
constexpr uint8_t kTargetFlagAltitudeRecorded = 1u << 2;
constexpr uint8_t kTargetFlagRelativeAltitudeValid = 1u << 3;

void put_offset(uint8_t* out, uint32_t utc_s, uint32_t base_utc_s) {
    const uint32_t elapsed = utc_s > base_utc_s ? utc_s - base_utc_s : 0;
    wire::put_uint16(
        out, static_cast<uint16_t>(elapsed > kTimeOffsetCeilingS ? kTimeOffsetCeilingS : elapsed));
}

void seal(uint8_t* out) {
    wire::put_uint16(out + kChecksummedBytes, wire::crc16_ccitt(out, kChecksummedBytes));
}

Status checked(const uint8_t* raw) {
    if (log_slot_erased(raw, kLogRecordBytes)) return Status::Empty;
    if (wire::crc16_ccitt(raw, kChecksummedBytes) != wire::get_uint16(raw + kChecksummedBytes))
        return Status::Crc;
    return Status::Ok;
}

bool event_slot(const uint8_t* raw) { return (raw[kLogKindOffset] & kLogEventMark) != 0; }

void encode_status(const LogStatus& status, uint8_t* out) {
    wire::put_uint16(out + 2, status.cell_mv);
    out[4] = status.percent;
    out[5] = status.power_level;
    out[6] = status.charge;
    out[7] = status.tracked;
    out[8] = status.tracked_peak;
    uint8_t flags = 0;
    if (status.battery_valid) flags |= kStatusFlagBatteryValid;
    if (status.charging) flags |= kStatusFlagCharging;
    if (status.external_power) flags |= kStatusFlagExternalPower;
    out[9] = flags;
}

void encode_target(const LogTarget& target, uint8_t* out) {
    const uint32_t address = target.address & settings::kAddressMask;
    wire::put_uint24(out + 2, address);
    out[5] = target.address_mapping_table;
    wire::put_uint16(out + 6, target.distance_m);
    wire::put_uint16(out + 8, static_cast<uint16_t>(target.relative_altitude_m));
    wire::put_uint16(out + 10, target.relative_bearing_deg);
    wire::put_uint16(out + 12, static_cast<uint16_t>(target.closing_mps));
    out[14] = target.alarm;
    out[15] = target.aircraft_category;
    out[17] = target.source;
    out[18] = static_cast<uint8_t>(target.rssi_dbm);
    uint8_t flags = 0;
    if (target.dismissed) flags |= kTargetFlagDismissed;
    if (target.in_formation) flags |= kTargetFlagInFormation;
    flags |= kTargetFlagAltitudeRecorded;
    if (target.relative_altitude_valid) flags |= kTargetFlagRelativeAltitudeValid;
    out[19] = flags;
    wire::put_uint16(out + 20, target.heard_s);
}

}  // namespace

LogRecord log_record_from(const model::Ownship& own) {
    LogRecord record{};
    record.utc_s = own.utc_s;
    record.latitude_1e7 = own.latitude_1e7;
    record.longitude_1e7 = own.longitude_1e7;
    record.altitude_msl_m = to_metres(Millimetres(own.altitude_msl_mm)).value;
    record.altitude_hae_m = to_metres(Millimetres(own.altitude_hae_mm)).value;
    record.speed_q = to_speed_q(MillimetresPerSecond(own.speed_mm_s)).value;
    record.track_c9 = to_cordic9(CentiDegrees(own.track_cdeg)).value;
    record.climb_e8 = to_climb_e8(MillimetresPerSecond(own.climb_mm_s)).value;
    record.hdop_e2 = own.hdop_e2;
    record.satellites = own.satellites;
    record.flight_state = own.flight_state;
    record.fix_valid = own.fix_valid;
    record.utc_valid = own.utc_valid;
    record.pps_locked = own.pps_locked;
    record.climb_valid = own.climb_valid;
    record.geoid_measured = own.geoid_measured;
    return record;
}

void seal_log_fix(const uint8_t* body, uint32_t utc_s, uint32_t base_utc_s, uint8_t* out) {
    std::memcpy(out + kFixBodyOffset, body, kLogFixBodyBytes);
    put_offset(out, utc_s, base_utc_s);
    seal(out);
}

void encode_log_fix_body(const LogRecord& record, uint8_t* body) {
    wire::put_uint32(body + 0, static_cast<uint32_t>(record.latitude_1e7));
    wire::put_uint32(body + 4, static_cast<uint32_t>(record.longitude_1e7));
    wire::put_uint16(body + 8, static_cast<uint16_t>(clamp_int16(record.altitude_msl_m)));
    // INFO: fc 20nov26 EGM96 runs -107 m to +85 m, so the geoid separation fits one signed byte
    body[10] = static_cast<uint8_t>(clamp_int8(record.altitude_hae_m - record.altitude_msl_m));
    wire::put_uint16(body + 11, record.speed_q);
    wire::put_uint16(body + 13, record.track_c9 & kTrackMask);
    wire::put_uint16(body + 15, static_cast<uint16_t>(record.climb_e8));
    body[17] = record.satellites;
    const uint32_t tenths = (record.hdop_e2 + 5u) / 10u;
    body[18] = static_cast<uint8_t>(tenths > kHdopTenthsCeiling ? kHdopTenthsCeiling : tenths);

    uint8_t flags = 0;
    if (record.fix_valid) flags |= kLogFlagFixValid;
    if (record.utc_valid) flags |= kLogFlagUtcValid;
    if (record.pps_locked) flags |= kLogFlagPpsLocked;
    if (record.climb_valid) flags |= kLogFlagClimbValid;
    if (record.geoid_measured) flags |= kLogFlagGeoidMeasured;
    if (record.session_end) flags |= kLogFlagSessionEnd;
    flags |= static_cast<uint8_t>((record.flight_state & kLogFlagFlightStateMask)
                                  << kLogFlagFlightStateShift);
    body[19] = flags;
}

bool log_slot_erased(const uint8_t* raw, uint32_t bytes) { return store::erased(raw, bytes); }

Status decode_log_record(const uint8_t* raw, uint32_t base_utc_s, LogRecord& out) {
    const Status slot = checked(raw);
    if (!is_ok(slot)) return slot;
    if (event_slot(raw)) return Status::Unsupported;

    out = LogRecord{};
    out.utc_s = base_utc_s + wire::get_uint16(raw + 0);
    out.latitude_1e7 = static_cast<int32_t>(wire::get_uint32(raw + 2));
    out.longitude_1e7 = static_cast<int32_t>(wire::get_uint32(raw + 6));
    out.altitude_msl_m = static_cast<int16_t>(wire::get_uint16(raw + 10));
    out.altitude_hae_m = out.altitude_msl_m + static_cast<int8_t>(raw[12]);
    out.speed_q = wire::get_uint16(raw + 13);
    out.track_c9 = wire::get_uint16(raw + 15);
    out.climb_e8 = static_cast<int16_t>(wire::get_uint16(raw + 17));
    out.satellites = raw[19];
    out.hdop_e2 = static_cast<uint16_t>(raw[20] * 10u);

    const uint8_t flags = raw[21];
    out.fix_valid = (flags & kLogFlagFixValid) != 0;
    out.utc_valid = (flags & kLogFlagUtcValid) != 0;
    out.pps_locked = (flags & kLogFlagPpsLocked) != 0;
    out.climb_valid = (flags & kLogFlagClimbValid) != 0;
    out.geoid_measured = (flags & kLogFlagGeoidMeasured) != 0;
    out.session_end = (flags & kLogFlagSessionEnd) != 0;
    out.flight_state =
        static_cast<uint8_t>((flags >> kLogFlagFlightStateShift) & kLogFlagFlightStateMask);
    return Status::Ok;
}

void encode_log_event(const LogEvent& event, uint32_t base_utc_s, uint8_t* out) {
    for (uint32_t i = 0; i < kLogRecordBytes; i++) out[i] = 0;
    put_offset(out + 0, event.utc_s, base_utc_s);
    if (event.kind == LogKind::Status)
        encode_status(event.status, out);
    else if (event.kind == LogKind::Bravo)
        out[2] = event.bravo;
    else
        encode_target(event.target, out);
    out[kLogKindOffset] = static_cast<uint8_t>(kLogEventMark | static_cast<uint8_t>(event.kind));
    seal(out);
}

}  // namespace skyblip::flight
