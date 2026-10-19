#include "core/flight/log_record.h"

#include "core/fec/crc.h"
#include "core/model/ownship.h"
#include "core/settings/address.h"
#include "core/store/sector.h"
#include "core/units/units.h"
#include "core/util/clamp.h"
#include "core/util/little_endian.h"

namespace skyblip::flight {

namespace {

constexpr uint32_t kTimeOffsetCeilingS = 0xFFFF;
constexpr uint16_t kHdopTenthsCeiling = 255;
constexpr uint16_t kTrackMask = 0x01FF;
constexpr uint32_t kChecksummedBytes = kLogRecordBytes - 2;

constexpr uint8_t kStatusFlagBatteryValid = 1u << 0;
constexpr uint8_t kStatusFlagCharging = 1u << 1;
constexpr uint8_t kStatusFlagExternalPower = 1u << 2;
constexpr uint8_t kTargetFlagDismissed = 1u << 0;
constexpr uint8_t kTargetFlagInFormation = 1u << 1;

void put_offset(uint8_t* out, uint32_t utc_s, uint32_t base_utc_s) {
    const uint32_t elapsed = utc_s > base_utc_s ? utc_s - base_utc_s : 0;
    put_u16(out,
            static_cast<uint16_t>(elapsed > kTimeOffsetCeilingS ? kTimeOffsetCeilingS : elapsed));
}

void seal(uint8_t* out) {
    put_u16(out + kChecksummedBytes, fec::crc16_ccitt(out, kChecksummedBytes));
}

Status checked(const uint8_t* raw) {
    if (log_slot_erased(raw, kLogRecordBytes)) return Status::Empty;
    if (fec::crc16_ccitt(raw, kChecksummedBytes) != get_u16(raw + kChecksummedBytes))
        return Status::Crc;
    return Status::Ok;
}

bool event_slot(const uint8_t* raw) { return (raw[kLogKindOffset] & kLogEventMark) != 0; }

bool known_event(uint8_t code) {
    switch (static_cast<LogKind>(code)) {
        case LogKind::Status:
        case LogKind::Alarm:
        case LogKind::Encounter: return true;
        case LogKind::Fix: break;
    }
    return false;
}

void encode_status(const LogStatus& status, uint8_t* out) {
    put_u16(out + 2, status.cell_mv);
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

void decode_status(const uint8_t* raw, LogStatus& out) {
    out.cell_mv = get_u16(raw + 2);
    out.percent = raw[4];
    out.power_level = raw[5];
    out.charge = raw[6];
    out.tracked = raw[7];
    out.tracked_peak = raw[8];
    out.battery_valid = (raw[9] & kStatusFlagBatteryValid) != 0;
    out.charging = (raw[9] & kStatusFlagCharging) != 0;
    out.external_power = (raw[9] & kStatusFlagExternalPower) != 0;
}

void encode_target(const LogTarget& target, uint8_t* out) {
    const uint32_t address = target.address & settings::kAddressMask;
    out[2] = static_cast<uint8_t>(address);
    out[3] = static_cast<uint8_t>(address >> 8);
    out[4] = static_cast<uint8_t>(address >> 16);
    out[5] = target.address_mapping_table;
    put_u16(out + 6, target.distance_m);
    put_u16(out + 8, static_cast<uint16_t>(target.relative_altitude_m));
    put_u16(out + 10, target.relative_bearing_deg);
    put_u16(out + 12, static_cast<uint16_t>(target.closing_mps));
    out[14] = target.alarm;
    out[15] = target.aircraft_category;
    out[17] = target.source;
    out[18] = static_cast<uint8_t>(target.rssi_dbm);
    uint8_t flags = 0;
    if (target.dismissed) flags |= kTargetFlagDismissed;
    if (target.in_formation) flags |= kTargetFlagInFormation;
    out[19] = flags;
    put_u16(out + 20, target.heard_s);
}

void decode_target(const uint8_t* raw, LogTarget& out) {
    out.address = static_cast<uint32_t>(raw[2]) | (static_cast<uint32_t>(raw[3]) << 8) |
                  (static_cast<uint32_t>(raw[4]) << 16);
    out.address_mapping_table = raw[5];
    out.distance_m = get_u16(raw + 6);
    out.relative_altitude_m = static_cast<int16_t>(get_u16(raw + 8));
    out.relative_bearing_deg = get_u16(raw + 10);
    out.closing_mps = static_cast<int16_t>(get_u16(raw + 12));
    out.alarm = raw[14];
    out.aircraft_category = raw[15];
    out.source = raw[17];
    out.rssi_dbm = static_cast<int8_t>(raw[18]);
    out.dismissed = (raw[19] & kTargetFlagDismissed) != 0;
    out.in_formation = (raw[19] & kTargetFlagInFormation) != 0;
    out.heard_s = get_u16(raw + 20);
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

void encode_log_record(const LogRecord& record, uint32_t base_utc_s, uint8_t* out) {
    put_offset(out + 0, record.utc_s, base_utc_s);
    put_u32(out + 2, static_cast<uint32_t>(record.latitude_1e7));
    put_u32(out + 6, static_cast<uint32_t>(record.longitude_1e7));
    put_u16(out + 10, static_cast<uint16_t>(clamp_i16(record.altitude_msl_m)));
    // Height above the ellipsoid is stored as its distance from mean sea level:
    // EGM96 runs -107 m to +85 m over the whole planet, so one signed byte
    // carries the pair that two four-byte altitudes would have cost.
    out[12] = static_cast<uint8_t>(clamp_i8(record.altitude_hae_m - record.altitude_msl_m));
    put_u16(out + 13, record.speed_q);
    put_u16(out + 15, record.track_c9 & kTrackMask);
    put_u16(out + 17, static_cast<uint16_t>(record.climb_e8));
    out[19] = record.satellites;
    const uint32_t tenths = (record.hdop_e2 + 5u) / 10u;
    out[20] = static_cast<uint8_t>(tenths > kHdopTenthsCeiling ? kHdopTenthsCeiling : tenths);

    uint8_t flags = 0;
    if (record.fix_valid) flags |= kLogFlagFixValid;
    if (record.utc_valid) flags |= kLogFlagUtcValid;
    if (record.pps_locked) flags |= kLogFlagPpsLocked;
    if (record.climb_valid) flags |= kLogFlagClimbValid;
    if (record.geoid_measured) flags |= kLogFlagGeoidMeasured;
    if (record.session_end) flags |= kLogFlagSessionEnd;
    flags |= static_cast<uint8_t>((record.flight_state & kLogFlagFlightStateMask)
                                  << kLogFlagFlightStateShift);
    out[21] = flags;
    seal(out);
}

bool log_slot_erased(const uint8_t* raw, uint32_t bytes) { return store::erased(raw, bytes); }

Status decode_log_record(const uint8_t* raw, uint32_t base_utc_s, LogRecord& out) {
    const Status slot = checked(raw);
    if (!is_ok(slot)) return slot;
    if (event_slot(raw)) return Status::Unsupported;

    out = LogRecord{};
    out.utc_s = base_utc_s + get_u16(raw + 0);
    out.latitude_1e7 = static_cast<int32_t>(get_u32(raw + 2));
    out.longitude_1e7 = static_cast<int32_t>(get_u32(raw + 6));
    out.altitude_msl_m = static_cast<int16_t>(get_u16(raw + 10));
    out.altitude_hae_m = out.altitude_msl_m + static_cast<int8_t>(raw[12]);
    out.speed_q = get_u16(raw + 13);
    out.track_c9 = get_u16(raw + 15);
    out.climb_e8 = static_cast<int16_t>(get_u16(raw + 17));
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
    else
        encode_target(event.target, out);
    out[kLogKindOffset] = static_cast<uint8_t>(kLogEventMark | static_cast<uint8_t>(event.kind));
    seal(out);
}

Status decode_log_event(const uint8_t* raw, uint32_t base_utc_s, LogEvent& out) {
    const Status slot = checked(raw);
    if (!is_ok(slot)) return slot;
    const uint8_t code = static_cast<uint8_t>(raw[kLogKindOffset] & ~kLogEventMark);
    if (!event_slot(raw) || !known_event(code)) return Status::Unsupported;

    out = LogEvent{};
    out.kind = static_cast<LogKind>(code);
    out.utc_s = base_utc_s + get_u16(raw + 0);
    if (out.kind == LogKind::Status)
        decode_status(raw, out.status);
    else
        decode_target(raw, out.target);
    return Status::Ok;
}

}  // namespace skyblip::flight
