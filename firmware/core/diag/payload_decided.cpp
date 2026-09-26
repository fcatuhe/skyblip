#include "core/diag/payload.h"

namespace skyblip::diag {

Record record_of(const Boot& value, const Instant& at) {
    Record r = framed(Type::Boot, at);
    put_u32(r.payload + 0, value.capabilities);
    put_u32(r.payload + 4, value.fw_build);
    put_u16(r.payload + 8, value.fw_revision);
    r.payload[10] = value.fw_major;
    r.payload[11] = value.fw_minor;
    r.payload[12] = static_cast<uint8_t>(value.reset);
    r.payload[13] = static_cast<uint8_t>(value.image_state);
    return r;
}

bool read(const Record& record, Boot& out) {
    if (record.type != Type::Boot) return false;
    out = Boot{};
    out.capabilities = get_u32(record.payload + 0);
    out.fw_build = get_u32(record.payload + 4);
    out.fw_revision = get_u16(record.payload + 8);
    out.fw_major = record.payload[10];
    out.fw_minor = record.payload[11];
    out.reset = static_cast<power::ResetReason>(record.payload[12]);
    out.image_state = static_cast<dfu::ImageState>(record.payload[13]);
    return true;
}

Record record_of(const Config& value, const Instant& at) {
    Record r = framed(Type::Config, at);
    put_u32(r.payload + 0, value.addr);
    put_i16(r.payload + 4, value.battery_offset_mv);
    put_i16(r.payload + 6, value.freq_trim_e1_ppm);
    r.payload[8] = value.aircraft_type;
    r.payload[9] = value.addr_table;
    r.payload[10] = value.alarm_volume;
    r.payload[11] = value.settings_version;
    set_flag(r.flags, kConfigFlagAlarmEnabled, value.alarm_enabled);
    set_flag(r.flags, kConfigFlagMetric, value.metric);
    set_flag(r.flags, kConfigFlagBatteryTrimManual, value.battery_trim_manual);
    return r;
}

bool read(const Record& record, Config& out) {
    if (record.type != Type::Config) return false;
    out = Config{};
    out.addr = get_u32(record.payload + 0);
    out.battery_offset_mv = get_i16(record.payload + 4);
    out.freq_trim_e1_ppm = get_i16(record.payload + 6);
    out.aircraft_type = record.payload[8];
    out.addr_table = record.payload[9];
    out.alarm_volume = record.payload[10];
    out.settings_version = record.payload[11];
    out.alarm_enabled = record.flagged(kConfigFlagAlarmEnabled);
    out.metric = record.flagged(kConfigFlagMetric);
    out.battery_trim_manual = record.flagged(kConfigFlagBatteryTrimManual);
    return true;
}

Record record_of(const Dwell& value, const Instant& at) {
    Record r = framed(Type::Dwell, at);
    put_u32(r.payload + 0, value.freq_hz);
    put_u16(r.payload + 4, value.start_ms);
    put_u16(r.payload + 6, value.end_ms);
    put_u16(r.payload + 8, value.phase_ms);
    put_u16(r.payload + 10, value.duty_permille);
    r.payload[12] = static_cast<uint8_t>(value.state);
    r.payload[13] = static_cast<uint8_t>(value.band);
    r.payload[14] = static_cast<uint8_t>(value.refusal);
    put_i8(r.payload + 15, value.noise_dbm);
    set_flag(r.flags, kDwellFlagTxAllowed, value.tx_allowed);
    set_flag(r.flags, kDwellFlagOwnTxDwell, value.own_tx_dwell);
    set_flag(r.flags, kDwellFlagListenOnly, value.listen_only);
    set_flag(r.flags, kDwellFlagArmed, value.armed);
    set_flag(r.flags, kDwellFlagBurstArmed, value.burst_armed);
    return r;
}

bool read(const Record& record, Dwell& out) {
    if (record.type != Type::Dwell) return false;
    out = Dwell{};
    out.freq_hz = get_u32(record.payload + 0);
    out.start_ms = get_u16(record.payload + 4);
    out.end_ms = get_u16(record.payload + 6);
    out.phase_ms = get_u16(record.payload + 8);
    out.duty_permille = get_u16(record.payload + 10);
    out.state = static_cast<timing::SlotState>(record.payload[12]);
    out.band = static_cast<model::Band>(record.payload[13]);
    out.refusal = static_cast<Refusal>(record.payload[14]);
    out.noise_dbm = get_i8(record.payload + 15);
    out.tx_allowed = record.flagged(kDwellFlagTxAllowed);
    out.own_tx_dwell = record.flagged(kDwellFlagOwnTxDwell);
    out.listen_only = record.flagged(kDwellFlagListenOnly);
    out.armed = record.flagged(kDwellFlagArmed);
    out.burst_armed = record.flagged(kDwellFlagBurstArmed);
    return true;
}

Record record_of(const Flight& value, const Instant& at) {
    Record r = framed(Type::Flight, at);
    put_i32(r.payload + 0, value.speed_mm_s);
    put_i32(r.payload + 4, value.climb_mm_s);
    put_i16(r.payload + 8, value.alt_msl_m);
    put_u16(r.payload + 10, value.hdop_e2);
    put_u16(r.payload + 12, value.vdop_e2);
    r.payload[14] = static_cast<uint8_t>(value.declared);
    r.payload[15] = static_cast<uint8_t>(value.confirmed);
    set_flag(r.flags, kFlightFlagFixValid, value.fix_valid);
    set_flag(r.flags, kFlightFlagRolling, value.rolling);
    set_flag(r.flags, kFlightFlagClimbValid, value.climb_valid);
    set_flag(r.flags, kFlightFlagTxSettled, value.tx_settled);
    return r;
}

bool read(const Record& record, Flight& out) {
    if (record.type != Type::Flight) return false;
    out = Flight{};
    out.speed_mm_s = get_i32(record.payload + 0);
    out.climb_mm_s = get_i32(record.payload + 4);
    out.alt_msl_m = get_i16(record.payload + 8);
    out.hdop_e2 = get_u16(record.payload + 10);
    out.vdop_e2 = get_u16(record.payload + 12);
    out.declared = static_cast<flight::FlightState>(record.payload[14]);
    out.confirmed = static_cast<flight::FlightState>(record.payload[15]);
    out.fix_valid = record.flagged(kFlightFlagFixValid);
    out.rolling = record.flagged(kFlightFlagRolling);
    out.climb_valid = record.flagged(kFlightFlagClimbValid);
    out.tx_settled = record.flagged(kFlightFlagTxSettled);
    return true;
}

Record record_of(const Traffic& value, const Instant& at) {
    Record r = framed(Type::Traffic, at);
    put_u32(r.payload + 0, value.addr);
    put_u16(r.payload + 4, value.dist_m);
    put_u16(r.payload + 6, value.bearing_deg);
    put_i16(r.payload + 8, value.rel_alt_m);
    put_i16(r.payload + 10, value.closing_mps);
    r.payload[12] = static_cast<uint8_t>(value.alarm);
    r.payload[13] = static_cast<uint8_t>(value.source);
    put_i8(r.payload + 14, value.rssi_dbm);
    r.payload[15] = value.tracked;
    set_flag(r.flags, kTrafficFlagAssessed, value.assessed);
    set_flag(r.flags, kTrafficFlagDismissed, value.dismissed);
    set_flag(r.flags, kTrafficFlagFormation, value.in_formation);
    set_flag(r.flags, kTrafficFlagPositionValid, value.position_valid);
    return r;
}

bool read(const Record& record, Traffic& out) {
    if (record.type != Type::Traffic) return false;
    out = Traffic{};
    out.addr = get_u32(record.payload + 0);
    out.dist_m = get_u16(record.payload + 4);
    out.bearing_deg = get_u16(record.payload + 6);
    out.rel_alt_m = get_i16(record.payload + 8);
    out.closing_mps = get_i16(record.payload + 10);
    out.alarm = static_cast<traffic::Level>(record.payload[12]);
    out.source = static_cast<model::Source>(record.payload[13]);
    out.rssi_dbm = get_i8(record.payload + 14);
    out.tracked = record.payload[15];
    out.assessed = record.flagged(kTrafficFlagAssessed);
    out.dismissed = record.flagged(kTrafficFlagDismissed);
    out.in_formation = record.flagged(kTrafficFlagFormation);
    out.position_valid = record.flagged(kTrafficFlagPositionValid);
    return true;
}

Record record_of(const Write& value, const Instant& at) {
    Record r = framed(Type::Write, at);
    put_u32(r.payload + 0, value.waited_ms);
    put_u16(r.payload + 4, value.phase_ms);
    put_u16(r.payload + 6, clamp_u16(value.requests));
    put_u16(r.payload + 8, clamp_u16(value.writes));
    put_u16(r.payload + 10, clamp_u16(value.forced));
    r.payload[12] = static_cast<uint8_t>(value.placement);
    r.payload[13] = static_cast<uint8_t>(value.kind);
    set_flag(r.flags, kWriteFlagPending, value.pending);
    return r;
}

bool read(const Record& record, Write& out) {
    if (record.type != Type::Write) return false;
    out = Write{};
    out.waited_ms = get_u32(record.payload + 0);
    out.phase_ms = get_u16(record.payload + 4);
    out.requests = get_u16(record.payload + 6);
    out.writes = get_u16(record.payload + 8);
    out.forced = get_u16(record.payload + 10);
    out.placement = static_cast<timing::DurableWriteVerdict>(record.payload[12]);
    out.kind = static_cast<power::DurableWrite>(record.payload[13]);
    out.pending = record.flagged(kWriteFlagPending);
    return true;
}

Record record_of(const Screen& value, const Instant& at) {
    Record r = framed(Type::Screen, at);
    put_u32(r.payload + 0, value.since_ms);
    r.payload[4] = value.page;
    r.payload[5] = value.mode;
    r.payload[6] = value.prompt;
    r.payload[7] = static_cast<uint8_t>(value.alarm);
    set_flag(r.flags, kScreenFlagBacklight, value.backlight);
    set_flag(r.flags, kScreenFlagPowered, value.powered);
    set_flag(r.flags, kScreenFlagHolding, value.holding);
    return r;
}

bool read(const Record& record, Screen& out) {
    if (record.type != Type::Screen) return false;
    out = Screen{};
    out.since_ms = get_u32(record.payload + 0);
    out.page = record.payload[4];
    out.mode = record.payload[5];
    out.prompt = record.payload[6];
    out.alarm = static_cast<traffic::Level>(record.payload[7]);
    out.backlight = record.flagged(kScreenFlagBacklight);
    out.powered = record.flagged(kScreenFlagPowered);
    out.holding = record.flagged(kScreenFlagHolding);
    return true;
}

Record record_of(const Gap& value, const Instant& at) {
    Record r = framed(Type::Gap, at);
    put_u32(r.payload + 0, value.dropped);
    put_u32(r.payload + 4, value.span_ms);
    put_u32(r.payload + 8, value.total);
    put_u16(r.payload + 12, value.capacity);
    return r;
}

bool read(const Record& record, Gap& out) {
    if (record.type != Type::Gap) return false;
    out = Gap{};
    out.dropped = get_u32(record.payload + 0);
    out.span_ms = get_u32(record.payload + 4);
    out.total = get_u32(record.payload + 8);
    out.capacity = get_u16(record.payload + 12);
    return true;
}

Record record_of(const End& value, const Instant& at) {
    Record r = framed(Type::End, at);
    put_u32(r.payload + 0, value.records);
    put_u32(r.payload + 4, value.dropped);
    return r;
}

bool read(const Record& record, End& out) {
    if (record.type != Type::End) return false;
    out = End{};
    out.records = get_u32(record.payload + 0);
    out.dropped = get_u32(record.payload + 4);
    return true;
}

Record record_of(const Duty& value, const Instant& at) {
    Record r = framed(Type::Duty, at);
    // INFO: fc 21sep26 the low half, not clamp_u16: readers subtract, and a clamp loses the delta
    put_u16(r.payload + 0, static_cast<uint16_t>(value.panel_partial_refreshes));
    put_u16(r.payload + 2, static_cast<uint16_t>(value.panel_full_refreshes));
    put_u16(r.payload + 4, static_cast<uint16_t>(value.backlight_ms));
    put_u16(r.payload + 6, static_cast<uint16_t>(value.rx_armed_ms));
    put_u16(r.payload + 8, static_cast<uint16_t>(value.tx_keyed_ms));
    put_u16(r.payload + 10, static_cast<uint16_t>(value.ble_connected_ms));
    put_u16(r.payload + 12, static_cast<uint16_t>(value.annunciator_ms));
    return r;
}

bool read(const Record& record, Duty& out) {
    if (record.type != Type::Duty) return false;
    out = Duty{};
    out.panel_partial_refreshes = get_u16(record.payload + 0);
    out.panel_full_refreshes = get_u16(record.payload + 2);
    out.backlight_ms = get_u16(record.payload + 4);
    out.rx_armed_ms = get_u16(record.payload + 6);
    out.tx_keyed_ms = get_u16(record.payload + 8);
    out.ble_connected_ms = get_u16(record.payload + 10);
    out.annunciator_ms = get_u16(record.payload + 12);
    return true;
}

}  // namespace skyblip::diag
