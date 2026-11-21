// Harness, not a test: the decoder of the flight log's event records. The device never reads one
// back.
#ifndef SKYBLIP_TEST_SUPPORT_LOG_EVENT_READ_H
#define SKYBLIP_TEST_SUPPORT_LOG_EVENT_READ_H

#include <cstdint>

#include "core/flight/log_record.h"
#include "core/util/result.h"
#include "protocols/common/little_endian.h"

namespace skyblip::flight {

constexpr uint8_t kStatusFlagBatteryValid = 1u << 0;
constexpr uint8_t kStatusFlagCharging = 1u << 1;
constexpr uint8_t kStatusFlagExternalPower = 1u << 2;
constexpr uint8_t kTargetFlagDismissed = 1u << 0;
constexpr uint8_t kTargetFlagInFormation = 1u << 1;
constexpr uint8_t kTargetFlagRelativeAltitudeValid = 1u << 3;

inline bool known_event(uint8_t code) {
    switch (static_cast<LogKind>(code)) {
        case LogKind::Status:
        case LogKind::Alarm:
        case LogKind::Encounter:
        case LogKind::Bravo: return true;
        case LogKind::Fix: break;
    }
    return false;
}

inline void decode_status(const uint8_t* raw, LogStatus& out) {
    out.cell_mv = wire::get_uint16(raw + 2);
    out.percent = raw[4];
    out.power_level = raw[5];
    out.charge = raw[6];
    out.tracked = raw[7];
    out.tracked_peak = raw[8];
    out.battery_valid = (raw[9] & kStatusFlagBatteryValid) != 0;
    out.charging = (raw[9] & kStatusFlagCharging) != 0;
    out.external_power = (raw[9] & kStatusFlagExternalPower) != 0;
}

inline void decode_target(const uint8_t* raw, LogTarget& out) {
    out.address = static_cast<uint32_t>(raw[2]) | (static_cast<uint32_t>(raw[3]) << 8) |
                  (static_cast<uint32_t>(raw[4]) << 16);
    out.address_mapping_table = raw[5];
    out.distance_m = wire::get_uint16(raw + 6);
    out.relative_altitude_m = static_cast<int16_t>(wire::get_uint16(raw + 8));
    out.relative_bearing_deg = wire::get_uint16(raw + 10);
    out.closing_mps = static_cast<int16_t>(wire::get_uint16(raw + 12));
    out.alarm = raw[14];
    out.aircraft_category = raw[15];
    out.source = raw[17];
    out.rssi_dbm = static_cast<int8_t>(raw[18]);
    out.dismissed = (raw[19] & kTargetFlagDismissed) != 0;
    out.in_formation = (raw[19] & kTargetFlagInFormation) != 0;
    out.relative_altitude_valid = (raw[19] & kTargetFlagRelativeAltitudeValid) != 0;
    out.heard_s = wire::get_uint16(raw + 20);
}

// INFO: fc 15oct26 Unsupported for a fix or an unknown kind, so neither is read as the other
inline Status decode_log_event(const uint8_t* raw, uint32_t base_utc_s, LogEvent& out) {
    LogRecord fix{};
    const Status slot = decode_log_record(raw, base_utc_s, fix);
    if (slot != Status::Unsupported) return is_ok(slot) ? Status::Unsupported : slot;
    const uint8_t code = static_cast<uint8_t>(raw[kLogKindOffset] & ~kLogEventMark);
    if (!known_event(code)) return Status::Unsupported;

    out = LogEvent{};
    out.kind = static_cast<LogKind>(code);
    out.utc_s = base_utc_s + wire::get_uint16(raw + 0);
    if (out.kind == LogKind::Status)
        decode_status(raw, out.status);
    else if (out.kind == LogKind::Bravo)
        out.bravo = raw[2];
    else
        decode_target(raw, out.target);
    return Status::Ok;
}

}  // namespace skyblip::flight

#endif
