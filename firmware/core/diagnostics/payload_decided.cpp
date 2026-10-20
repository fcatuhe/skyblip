#include "core/diagnostics/payload.h"

namespace skyblip::diagnostics {

Record record_of(const Boot& value, const Instant& at) {
    Record record = framed(Type::Boot, at);
    put_uint32(record.payload + 0, value.capabilities);
    put_uint32(record.payload + 4, value.firmware_build);
    put_uint16(record.payload + 8, value.firmware_revision);
    record.payload[10] = value.firmware_major;
    record.payload[11] = value.firmware_minor;
    record.payload[12] = static_cast<uint8_t>(value.reset);
    record.payload[13] = static_cast<uint8_t>(value.image_state);
    record.payload[14] = static_cast<uint8_t>(value.profile);
    set_flag(record.flags, kBootFlagProfileRecorded, value.profile_recorded);
    return record;
}

bool read(const Record& record, Boot& out) {
    if (record.type != Type::Boot) return false;
    out = Boot{};
    out.capabilities = get_uint32(record.payload + 0);
    out.firmware_build = get_uint32(record.payload + 4);
    out.firmware_revision = get_uint16(record.payload + 8);
    out.firmware_major = record.payload[10];
    out.firmware_minor = record.payload[11];
    out.reset = static_cast<power::ResetReason>(record.payload[12]);
    out.image_state = static_cast<dfu::ImageState>(record.payload[13]);
    out.profile = static_cast<Profile>(record.payload[14]);
    out.profile_recorded = record.flagged(kBootFlagProfileRecorded);
    return true;
}

Record record_of(const Config& value, const Instant& at) {
    Record record = framed(Type::Config, at);
    put_uint32(record.payload + 0, value.address);
    put_int16(record.payload + 4, value.battery_offset_mv);
    put_int16(record.payload + 6, value.frequency_trim_e1_ppm);
    record.payload[8] = value.aircraft_type;
    record.payload[9] = value.address_mapping_table;
    record.payload[10] = value.alarm_volume;
    record.payload[11] = value.settings_version;
    put_int8(record.payload + 12, value.tx_power_dbm);
    put_int8(record.payload + 13, value.pa_rated_dbm);
    set_flag(record.flags, kConfigFlagAlarmEnabled, value.alarm_enabled);
    set_flag(record.flags, kConfigFlagMetric, value.metric);
    set_flag(record.flags, kConfigFlagBatteryTrimManual, value.battery_trim_manual);
    return record;
}

bool read(const Record& record, Config& out) {
    if (record.type != Type::Config) return false;
    out = Config{};
    out.address = get_uint32(record.payload + 0);
    out.battery_offset_mv = get_int16(record.payload + 4);
    out.frequency_trim_e1_ppm = get_int16(record.payload + 6);
    out.aircraft_type = record.payload[8];
    out.address_mapping_table = record.payload[9];
    out.alarm_volume = record.payload[10];
    out.settings_version = record.payload[11];
    out.tx_power_dbm = get_int8(record.payload + 12);
    out.pa_rated_dbm = get_int8(record.payload + 13);
    out.alarm_enabled = record.flagged(kConfigFlagAlarmEnabled);
    out.metric = record.flagged(kConfigFlagMetric);
    out.battery_trim_manual = record.flagged(kConfigFlagBatteryTrimManual);
    return true;
}

Record record_of(const Dwell& value, const Instant& at) {
    Record record = framed(Type::Dwell, at);
    put_uint32(record.payload + 0, value.frequency_hz);
    put_uint16(record.payload + 4, value.start_ms);
    put_uint16(record.payload + 6, value.end_ms);
    put_uint16(record.payload + 8, value.phase_ms);
    put_uint16(record.payload + 10, value.duty_permille);
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

bool read(const Record& record, Dwell& out) {
    if (record.type != Type::Dwell) return false;
    out = Dwell{};
    out.frequency_hz = get_uint32(record.payload + 0);
    out.start_ms = get_uint16(record.payload + 4);
    out.end_ms = get_uint16(record.payload + 6);
    out.phase_ms = get_uint16(record.payload + 8);
    out.duty_permille = get_uint16(record.payload + 10);
    out.state = static_cast<timing::SlotState>(record.payload[12]);
    out.band = static_cast<model::Band>(record.payload[13]);
    out.refusal = static_cast<Refusal>(record.payload[14]);
    out.noise_dbm = get_int8(record.payload + 15);
    out.tx_allowed = record.flagged(kDwellFlagTxAllowed);
    out.own_tx_dwell = record.flagged(kDwellFlagOwnTxDwell);
    out.listen_only = record.flagged(kDwellFlagListenOnly);
    out.armed = record.flagged(kDwellFlagArmed);
    out.burst_armed = record.flagged(kDwellFlagBurstArmed);
    return true;
}

Record record_of(const Flight& value, const Instant& at) {
    Record record = framed(Type::Flight, at);
    put_int32(record.payload + 0, value.speed_mm_s);
    put_int32(record.payload + 4, value.climb_mm_s);
    put_int16(record.payload + 8, value.altitude_msl_m);
    put_uint16(record.payload + 10, value.hdop_e2);
    put_uint16(record.payload + 12, value.vdop_e2);
    record.payload[14] = static_cast<uint8_t>(value.declared);
    record.payload[15] = static_cast<uint8_t>(value.confirmed);
    set_flag(record.flags, kFlightFlagFixValid, value.fix_valid);
    set_flag(record.flags, kFlightFlagRolling, value.rolling);
    set_flag(record.flags, kFlightFlagClimbValid, value.climb_valid);
    set_flag(record.flags, kFlightFlagTxSettled, value.tx_settled);
    return record;
}

bool read(const Record& record, Flight& out) {
    if (record.type != Type::Flight) return false;
    out = Flight{};
    out.speed_mm_s = get_int32(record.payload + 0);
    out.climb_mm_s = get_int32(record.payload + 4);
    out.altitude_msl_m = get_int16(record.payload + 8);
    out.hdop_e2 = get_uint16(record.payload + 10);
    out.vdop_e2 = get_uint16(record.payload + 12);
    out.declared = static_cast<flight::FlightState>(record.payload[14]);
    out.confirmed = static_cast<flight::FlightState>(record.payload[15]);
    out.fix_valid = record.flagged(kFlightFlagFixValid);
    out.rolling = record.flagged(kFlightFlagRolling);
    out.climb_valid = record.flagged(kFlightFlagClimbValid);
    out.tx_settled = record.flagged(kFlightFlagTxSettled);
    return true;
}

Record record_of(const Traffic& value, const Instant& at) {
    Record record = framed(Type::Traffic, at);
    put_uint32(record.payload + 0, value.address);
    put_uint16(record.payload + 4, value.distance_m);
    put_uint16(record.payload + 6, value.bearing_deg);
    put_int16(record.payload + 8, value.relative_altitude_m);
    put_int16(record.payload + 10, value.closing_mps);
    record.payload[12] = static_cast<uint8_t>(value.alarm);
    record.payload[13] = static_cast<uint8_t>(value.source);
    put_int8(record.payload + 14, value.rssi_dbm);
    record.payload[15] = value.tracked;
    set_flag(record.flags, kTrafficFlagAssessed, value.assessed);
    set_flag(record.flags, kTrafficFlagDismissed, value.dismissed);
    set_flag(record.flags, kTrafficFlagInFormation, value.in_formation);
    set_flag(record.flags, kTrafficFlagPositionValid, value.position_valid);
    return record;
}

bool read(const Record& record, Traffic& out) {
    if (record.type != Type::Traffic) return false;
    out = Traffic{};
    out.address = get_uint32(record.payload + 0);
    out.distance_m = get_uint16(record.payload + 4);
    out.bearing_deg = get_uint16(record.payload + 6);
    out.relative_altitude_m = get_int16(record.payload + 8);
    out.closing_mps = get_int16(record.payload + 10);
    out.alarm = static_cast<traffic::Level>(record.payload[12]);
    out.source = static_cast<model::Source>(record.payload[13]);
    out.rssi_dbm = get_int8(record.payload + 14);
    out.tracked = record.payload[15];
    out.assessed = record.flagged(kTrafficFlagAssessed);
    out.dismissed = record.flagged(kTrafficFlagDismissed);
    out.in_formation = record.flagged(kTrafficFlagInFormation);
    out.position_valid = record.flagged(kTrafficFlagPositionValid);
    return true;
}

Record record_of(const Write& value, const Instant& at) {
    Record record = framed(Type::Write, at);
    put_uint32(record.payload + 0, value.waited_ms);
    put_uint16(record.payload + 4, value.phase_ms);
    put_uint16(record.payload + 6, clamp_uint16(value.requests));
    put_uint16(record.payload + 8, clamp_uint16(value.writes));
    put_uint16(record.payload + 10, clamp_uint16(value.forced));
    record.payload[12] = static_cast<uint8_t>(value.placement);
    record.payload[13] = static_cast<uint8_t>(value.kind);
    set_flag(record.flags, kWriteFlagPending, value.pending);
    return record;
}

bool read(const Record& record, Write& out) {
    if (record.type != Type::Write) return false;
    out = Write{};
    out.waited_ms = get_uint32(record.payload + 0);
    out.phase_ms = get_uint16(record.payload + 4);
    out.requests = get_uint16(record.payload + 6);
    out.writes = get_uint16(record.payload + 8);
    out.forced = get_uint16(record.payload + 10);
    out.placement = static_cast<timing::DurableWriteVerdict>(record.payload[12]);
    out.kind = static_cast<power::DurableWrite>(record.payload[13]);
    out.pending = record.flagged(kWriteFlagPending);
    return true;
}

Record record_of(const Screen& value, const Instant& at) {
    Record record = framed(Type::Screen, at);
    put_uint32(record.payload + 0, value.since_page_shown_ms);
    record.payload[4] = value.page;
    record.payload[5] = value.mode;
    record.payload[6] = value.prompt;
    record.payload[7] = static_cast<uint8_t>(value.alarm);
    put_uint32(record.payload + 8, value.render_us);
    put_uint32(record.payload + 12, value.present_us);
    set_flag(record.flags, kScreenFlagBacklight, value.backlight);
    set_flag(record.flags, kScreenFlagPowered, value.powered);
    set_flag(record.flags, kScreenFlagHolding, value.holding);
    return record;
}

bool read(const Record& record, Screen& out) {
    if (record.type != Type::Screen) return false;
    out = Screen{};
    out.since_page_shown_ms = get_uint32(record.payload + 0);
    out.page = record.payload[4];
    out.mode = record.payload[5];
    out.prompt = record.payload[6];
    out.alarm = static_cast<traffic::Level>(record.payload[7]);
    out.render_us = get_uint32(record.payload + 8);
    out.present_us = get_uint32(record.payload + 12);
    out.backlight = record.flagged(kScreenFlagBacklight);
    out.powered = record.flagged(kScreenFlagPowered);
    out.holding = record.flagged(kScreenFlagHolding);
    return true;
}

Record record_of(const Loop& value, const Instant& at) {
    Record record = framed(Type::Loop, at);
    put_uint16(record.payload + 0, value.passes);
    put_uint16(record.payload + 2, value.busy_ms);
    put_uint32(record.payload + 4, value.worst_pass_us);
    put_uint16(record.payload + 8, value.worst_pass_phase_ms);
    put_uint16(record.payload + 10, value.worst_gap_ms);
    put_uint16(record.payload + 12, value.worst_gap_phase_ms);
    record.payload[14] = value.worst_service;
    record.payload[15] = value.worst_tick_ms;
    return record;
}

bool read(const Record& record, Loop& out) {
    if (record.type != Type::Loop) return false;
    out = Loop{};
    out.passes = get_uint16(record.payload + 0);
    out.busy_ms = get_uint16(record.payload + 2);
    out.worst_pass_us = get_uint32(record.payload + 4);
    out.worst_pass_phase_ms = get_uint16(record.payload + 8);
    out.worst_gap_ms = get_uint16(record.payload + 10);
    out.worst_gap_phase_ms = get_uint16(record.payload + 12);
    out.worst_service = record.payload[14];
    out.worst_tick_ms = record.payload[15];
    return true;
}

Record record_of(const Gap& value, const Instant& at) {
    Record record = framed(Type::Gap, at);
    put_uint32(record.payload + 0, value.dropped);
    put_uint32(record.payload + 4, value.span_ms);
    put_uint32(record.payload + 8, value.total);
    put_uint16(record.payload + 12, value.capacity);
    return record;
}

bool read(const Record& record, Gap& out) {
    if (record.type != Type::Gap) return false;
    out = Gap{};
    out.dropped = get_uint32(record.payload + 0);
    out.span_ms = get_uint32(record.payload + 4);
    out.total = get_uint32(record.payload + 8);
    out.capacity = get_uint16(record.payload + 12);
    return true;
}

Record record_of(const End& value, const Instant& at) {
    Record record = framed(Type::End, at);
    put_uint32(record.payload + 0, value.records);
    put_uint32(record.payload + 4, value.dropped);
    return record;
}

bool read(const Record& record, End& out) {
    if (record.type != Type::End) return false;
    out = End{};
    out.records = get_uint32(record.payload + 0);
    out.dropped = get_uint32(record.payload + 4);
    return true;
}

Record record_of(const Duty& value, const Instant& at) {
    Record record = framed(Type::Duty, at);
    // INFO: fc 21sep26 the low half, not clamp_uint16: readers subtract, and a clamp loses the
    // delta
    put_uint16(record.payload + 0, static_cast<uint16_t>(value.panel_partial_refreshes));
    put_uint16(record.payload + 2, static_cast<uint16_t>(value.panel_full_refreshes));
    put_uint16(record.payload + 4, static_cast<uint16_t>(value.backlight_ms));
    put_uint16(record.payload + 6, static_cast<uint16_t>(value.rx_armed_ms));
    put_uint16(record.payload + 8, static_cast<uint16_t>(value.tx_keyed_ms));
    put_uint16(record.payload + 10, static_cast<uint16_t>(value.ble_connected_ms));
    put_uint16(record.payload + 12, static_cast<uint16_t>(value.annunciator_ms));
    return record;
}

bool read(const Record& record, Duty& out) {
    if (record.type != Type::Duty) return false;
    out = Duty{};
    out.panel_partial_refreshes = get_uint16(record.payload + 0);
    out.panel_full_refreshes = get_uint16(record.payload + 2);
    out.backlight_ms = get_uint16(record.payload + 4);
    out.rx_armed_ms = get_uint16(record.payload + 6);
    out.tx_keyed_ms = get_uint16(record.payload + 8);
    out.ble_connected_ms = get_uint16(record.payload + 10);
    out.annunciator_ms = get_uint16(record.payload + 12);
    return true;
}

}  // namespace skyblip::diagnostics
