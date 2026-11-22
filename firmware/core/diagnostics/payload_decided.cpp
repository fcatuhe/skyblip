#include "core/diagnostics/payload.h"
#include "protocols/common/little_endian.h"

namespace skyblip::diagnostics {

Record record_of(const Boot& value, const Instant& at) {
    Record record = framed(Type::Boot, at);
    wire::put_uint32(record.payload + 0, value.capabilities);
    wire::put_uint32(record.payload + 4, value.firmware_build);
    wire::put_uint16(record.payload + 8, value.firmware_revision);
    record.payload[10] = value.firmware_major;
    record.payload[11] = value.firmware_minor;
    record.payload[12] = static_cast<uint8_t>(value.reset);
    record.payload[13] = static_cast<uint8_t>(value.image_state);
    record.payload[14] = static_cast<uint8_t>(value.profile);
    set_flag(record.flags, kBootFlagProfileRecorded, value.profile_recorded);
    return record;
}

Record record_of(const Config& value, const Instant& at) {
    Record record = framed(Type::Config, at);
    wire::put_uint32(record.payload + 0, value.address);
    wire::put_int16(record.payload + 4, value.battery_offset_mv);
    wire::put_int16(record.payload + 6, value.frequency_trim_e1_ppm);
    record.payload[8] = value.aircraft_type;
    record.payload[9] = value.address_mapping_table;
    record.payload[11] = value.settings_version;
    put_int8(record.payload + 12, value.tx_power_dbm);
    put_int8(record.payload + 13, value.pa_rated_dbm);
    record.payload[14] = value.alarm_setting;
    set_flag(record.flags, kConfigFlagAlarmSettingRecorded, value.alarm_setting_recorded);
    set_flag(record.flags, kConfigFlagMetric, value.metric);
    set_flag(record.flags, kConfigFlagBatteryTrimManual, value.battery_trim_manual);
    return record;
}

Record record_of(const Dwell& value, const Instant& at) {
    Record record = framed(Type::Dwell, at);
    wire::put_uint32(record.payload + 0, value.frequency_hz);
    wire::put_uint16(record.payload + 4, value.start_ms);
    wire::put_uint16(record.payload + 6, value.end_ms);
    wire::put_uint16(record.payload + 8, value.phase_ms);
    wire::put_uint16(record.payload + 10, value.duty_permille);
    record.payload[12] = static_cast<uint8_t>(value.state);
    record.payload[13] = static_cast<uint8_t>(value.band);
    record.payload[14] = static_cast<uint8_t>(value.refusal);
    put_int8(record.payload + 15, value.noise_dbm);
    set_flag(record.flags, kDwellFlagTxAllowed, value.tx_allowed);
    set_flag(record.flags, kDwellFlagOwnTxDwell, value.own_tx_dwell);
    set_flag(record.flags, kDwellFlagListenOnly, value.listen_only);
    set_flag(record.flags, kDwellFlagArmed, value.armed);
    set_flag(record.flags, kDwellFlagBurstArmed, value.burst_armed);
    return record;
}

Record record_of(const Flight& value, const Instant& at) {
    Record record = framed(Type::Flight, at);
    wire::put_int32(record.payload + 0, value.speed_mm_s);
    wire::put_int32(record.payload + 4, value.climb_mm_s);
    wire::put_int16(record.payload + 8, value.altitude_msl_m);
    wire::put_uint16(record.payload + 10, value.hdop_e2);
    wire::put_uint16(record.payload + 12, value.vdop_e2);
    record.payload[14] = static_cast<uint8_t>(value.declared);
    record.payload[15] = static_cast<uint8_t>(value.confirmed);
    set_flag(record.flags, kFlightFlagFixValid, value.fix_valid);
    set_flag(record.flags, kFlightFlagRolling, value.rolling);
    set_flag(record.flags, kFlightFlagClimbValid, value.climb_valid);
    set_flag(record.flags, kFlightFlagTxSettled, value.tx_settled);
    return record;
}

Record record_of(const Traffic& value, const Instant& at) {
    Record record = framed(Type::Traffic, at);
    wire::put_uint32(record.payload + 0, value.address);
    wire::put_uint16(record.payload + 4, value.distance_m);
    wire::put_uint16(record.payload + 6, value.bearing_deg);
    wire::put_int16(record.payload + 8, value.relative_altitude_m);
    wire::put_int16(record.payload + 10, value.closing_mps);
    record.payload[12] = static_cast<uint8_t>(value.alarm);
    record.payload[13] = static_cast<uint8_t>(value.source);
    put_int8(record.payload + 14, value.rssi_dbm);
    record.payload[15] = value.tracked;
    set_flag(record.flags, kTrafficFlagAssessed, value.assessed);
    set_flag(record.flags, kTrafficFlagDismissed, value.dismissed);
    set_flag(record.flags, kTrafficFlagInFormation, value.in_formation);
    set_flag(record.flags, kTrafficFlagPositionValid, value.position_valid);
    set_flag(record.flags, kTrafficFlagAltitudeRecorded, true);
    set_flag(record.flags, kTrafficFlagRelativeAltitudeValid, value.relative_altitude_valid);
    return record;
}

Record record_of(const Write& value, const Instant& at) {
    Record record = framed(Type::Write, at);
    wire::put_uint32(record.payload + 0, value.waited_ms);
    wire::put_uint16(record.payload + 4, value.phase_ms);
    wire::put_uint16(record.payload + 6, clamp_uint16(value.requests));
    wire::put_uint16(record.payload + 8, clamp_uint16(value.writes));
    wire::put_uint16(record.payload + 10, clamp_uint16(value.forced));
    record.payload[12] = static_cast<uint8_t>(value.placement);
    record.payload[13] = static_cast<uint8_t>(value.kind);
    set_flag(record.flags, kWriteFlagPending, value.pending);
    return record;
}

Record record_of(const Screen& value, const Instant& at) {
    Record record = framed(Type::Screen, at);
    wire::put_uint32(record.payload + 0, value.since_page_shown_ms);
    record.payload[4] = value.page;
    record.payload[5] = value.mode;
    record.payload[6] = value.prompt;
    record.payload[7] = static_cast<uint8_t>(value.alarm);
    wire::put_uint32(record.payload + 8, value.render_us);
    wire::put_uint32(record.payload + 12, value.present_us);
    set_flag(record.flags, kScreenFlagBacklight, value.backlight);
    set_flag(record.flags, kScreenFlagPowered, value.powered);
    set_flag(record.flags, kScreenFlagHolding, value.holding);
    return record;
}

Record record_of(const Loop& value, const Instant& at) {
    Record record = framed(Type::Loop, at);
    wire::put_uint16(record.payload + 0, value.passes);
    wire::put_uint16(record.payload + 2, value.busy_ms);
    wire::put_uint32(record.payload + 4, value.worst_pass_us);
    wire::put_uint16(record.payload + 8, value.worst_pass_phase_ms);
    wire::put_uint16(record.payload + 10, value.worst_gap_ms);
    wire::put_uint16(record.payload + 12, value.worst_gap_phase_ms);
    record.payload[14] = static_cast<uint8_t>(value.worst_service);
    record.payload[15] = value.worst_tick_ms;
    return record;
}

Record record_of(const Gap& value, const Instant& at) {
    Record record = framed(Type::Gap, at);
    wire::put_uint32(record.payload + 0, value.dropped);
    wire::put_uint32(record.payload + 4, value.span_ms);
    wire::put_uint32(record.payload + 8, value.total);
    wire::put_uint16(record.payload + 12, value.capacity);
    return record;
}

Record record_of(const End& value, const Instant& at) {
    Record record = framed(Type::End, at);
    wire::put_uint32(record.payload + 0, value.records);
    wire::put_uint32(record.payload + 4, value.dropped);
    return record;
}

Record record_of(const Duty& value, const Instant& at) {
    Record record = framed(Type::Duty, at);
    // INFO: fc 21sep26 the low half, not clamp_uint16: readers subtract, and a clamp loses deltas
    wire::put_uint16(record.payload + 0, static_cast<uint16_t>(value.panel_partial_refreshes));
    wire::put_uint16(record.payload + 2, static_cast<uint16_t>(value.panel_full_refreshes));
    wire::put_uint16(record.payload + 4, static_cast<uint16_t>(value.backlight_ms));
    wire::put_uint16(record.payload + 6, static_cast<uint16_t>(value.rx_armed_ms));
    wire::put_uint16(record.payload + 8, static_cast<uint16_t>(value.tx_keyed_ms));
    wire::put_uint16(record.payload + 10, static_cast<uint16_t>(value.ble_connected_ms));
    wire::put_uint16(record.payload + 12, static_cast<uint16_t>(value.annunciator_ms));
    return record;
}

}  // namespace skyblip::diagnostics
