#include "core/diag/payload.h"

namespace skyblip::diag {

Record record_of(const Gnss& value, const Instant& at) {
    Record r = framed(Type::Gnss, at);
    put_u16(r.payload + 0, value.nav_ms);
    put_u16(r.payload + 2, value.resid_m);
    put_u16(r.payload + 4, value.hdop_e2);
    put_u16(r.payload + 6, value.vdop_e2);
    put_u16(r.payload + 8, clamp_u16(value.stage_s));
    r.payload[10] = value.sats;
    r.payload[11] = value.sats_in_view;
    r.payload[12] = value.fix_mode;
    r.payload[13] = static_cast<uint8_t>(value.reject);
    r.payload[14] = static_cast<uint8_t>(value.stage);
    set_flag(r.flags, kGnssFlagFixValid, value.fix_valid);
    set_flag(r.flags, kGnssFlagResidValid, value.resid_valid);
    set_flag(r.flags, kGnssFlagPpsLocked, value.pps_locked);
    set_flag(r.flags, kGnssFlagGeoidMeasured, value.geoid_measured);
    set_flag(r.flags, kGnssFlagTxSettled, value.tx_settled);
    set_flag(r.flags, kGnssFlagLevelsFresh, value.levels_fresh);
    return r;
}

bool read(const Record& record, Gnss& out) {
    if (record.type != Type::Gnss) return false;
    out = Gnss{};
    out.nav_ms = get_u16(record.payload + 0);
    out.resid_m = get_u16(record.payload + 2);
    out.hdop_e2 = get_u16(record.payload + 4);
    out.vdop_e2 = get_u16(record.payload + 6);
    out.stage_s = get_u16(record.payload + 8);
    out.sats = record.payload[10];
    out.sats_in_view = record.payload[11];
    out.fix_mode = record.payload[12];
    out.reject = static_cast<gnss::FixReject>(record.payload[13]);
    out.stage = static_cast<gnss::Stage>(record.payload[14]);
    out.fix_valid = record.flagged(kGnssFlagFixValid);
    out.resid_valid = record.flagged(kGnssFlagResidValid);
    out.pps_locked = record.flagged(kGnssFlagPpsLocked);
    out.geoid_measured = record.flagged(kGnssFlagGeoidMeasured);
    out.tx_settled = record.flagged(kGnssFlagTxSettled);
    out.levels_fresh = record.flagged(kGnssFlagLevelsFresh);
    return true;
}

Record record_of(const Pps& value, const Instant& at) {
    Record r = framed(Type::Pps, at);
    put_u32(r.payload + 0, value.interval_us);
    put_i32(r.payload + 4, value.error_us);
    put_u32(r.payload + 8, value.samples);
    put_u16(r.payload + 12, clamp_u16(value.holdover_events));
    put_u16(r.payload + 14, value.since_edge_ms);
    set_flag(r.flags, kPpsFlagLocked, value.locked);
    set_flag(r.flags, kPpsFlagUtcValid, value.utc_valid);
    return r;
}

bool read(const Record& record, Pps& out) {
    if (record.type != Type::Pps) return false;
    out = Pps{};
    out.interval_us = get_u32(record.payload + 0);
    out.error_us = get_i32(record.payload + 4);
    out.samples = get_u32(record.payload + 8);
    out.holdover_events = get_u16(record.payload + 12);
    out.since_edge_ms = get_u16(record.payload + 14);
    out.locked = record.flagged(kPpsFlagLocked);
    out.utc_valid = record.flagged(kPpsFlagUtcValid);
    return true;
}

Record record_of(const radio::Entry& value) {
    Instant at{};
    at.at_s = value.at_s;
    at.into_ms = value.into_ms;
    at.phase_valid = value.phase_valid;
    at.utc_dated = value.utc;
    Record r = framed(Type::Burst, at);
    put_u32(r.payload + 0, value.addr);
    put_u16(r.payload + 4, value.tx_keyed_us);
    put_u16(r.payload + 6, value.tx_span_us);
    r.payload[8] = static_cast<uint8_t>(value.event);
    r.payload[9] = static_cast<uint8_t>(value.source);
    r.payload[10] = static_cast<uint8_t>(value.band);
    r.payload[11] = value.channel;
    r.payload[12] = value.len;
    put_i8(r.payload + 13, value.rssi_dbm);
    put_i8(r.payload + 14, value.key_offset_s);
    set_flag(r.flags, kBurstFlagAddrValid, value.addr_valid);
    set_flag(r.flags, kBurstFlagRssiValid, value.rssi_valid);
    set_flag(r.flags, kBurstFlagAirborne, value.airborne);
    set_flag(r.flags, kBurstFlagTxSpanValid, value.tx_span_valid);
    set_flag(r.flags, kBurstFlagCallsign, value.callsign);
    return r;
}

bool read(const Record& record, radio::Entry& out) {
    if (record.type != Type::Burst) return false;
    out = radio::Entry{};
    out.at_s = record.at_s;
    out.into_ms = record.into_ms;
    out.phase_valid = record.phase_valid();
    out.utc = record.utc_dated();
    out.addr = get_u32(record.payload + 0);
    out.tx_keyed_us = get_u16(record.payload + 4);
    out.tx_span_us = get_u16(record.payload + 6);
    out.event = static_cast<radio::Event>(record.payload[8]);
    out.source = static_cast<model::Source>(record.payload[9]);
    out.band = static_cast<model::Band>(record.payload[10]);
    out.channel = record.payload[11];
    out.len = record.payload[12];
    out.rssi_dbm = get_i8(record.payload + 13);
    out.key_offset_s = get_i8(record.payload + 14);
    out.addr_valid = record.flagged(kBurstFlagAddrValid);
    out.rssi_valid = record.flagged(kBurstFlagRssiValid);
    out.airborne = record.flagged(kBurstFlagAirborne);
    out.tx_span_valid = record.flagged(kBurstFlagTxSpanValid);
    out.callsign = record.flagged(kBurstFlagCallsign);
    return true;
}

Record record_of(const Baro& value, const Instant& at) {
    Record r = framed(Type::Baro, at);
    put_u32(r.payload + 0, value.pressure_mpa);
    put_i32(r.payload + 4, value.alt_mm);
    put_i32(r.payload + 8, value.climb_mm_s);
    put_i16(r.payload + 12, value.temperature_dc);
    set_flag(r.flags, kBaroFlagActive, value.active);
    set_flag(r.flags, kBaroFlagTemperatureValid, value.temperature_valid);
    set_flag(r.flags, kBaroFlagClimbAdopted, value.climb_adopted);
    return r;
}

bool read(const Record& record, Baro& out) {
    if (record.type != Type::Baro) return false;
    out = Baro{};
    out.pressure_mpa = get_u32(record.payload + 0);
    out.alt_mm = get_i32(record.payload + 4);
    out.climb_mm_s = get_i32(record.payload + 8);
    out.temperature_dc = get_i16(record.payload + 12);
    out.active = record.flagged(kBaroFlagActive);
    out.temperature_valid = record.flagged(kBaroFlagTemperatureValid);
    out.climb_adopted = record.flagged(kBaroFlagClimbAdopted);
    return true;
}

Record record_of(const Motion& value, const Instant& at) {
    Record r = framed(Type::Motion, at);
    put_i16(r.payload + 0, value.slip_mg);
    put_i16(r.payload + 2, value.normal_mg);
    put_i16(r.payload + 4, value.lateral_mg);
    put_i16(r.payload + 6, value.longitudinal_mg);
    put_i16(r.payload + 8, value.most_normal_mg);
    put_i16(r.payload + 10, value.least_normal_mg);
    r.payload[12] = value.imu_error;
    r.payload[13] = value.sensor_error;
    set_flag(r.flags, kMotionFlagSlipValid, value.slip_valid);
    set_flag(r.flags, kMotionFlagGLoadValid, value.gload_valid);
    set_flag(r.flags, kMotionFlagFitted, value.fitted);
    return r;
}

bool read(const Record& record, Motion& out) {
    if (record.type != Type::Motion) return false;
    out = Motion{};
    out.slip_mg = get_i16(record.payload + 0);
    out.normal_mg = get_i16(record.payload + 2);
    out.lateral_mg = get_i16(record.payload + 4);
    out.longitudinal_mg = get_i16(record.payload + 6);
    out.most_normal_mg = get_i16(record.payload + 8);
    out.least_normal_mg = get_i16(record.payload + 10);
    out.imu_error = record.payload[12];
    out.sensor_error = record.payload[13];
    out.slip_valid = record.flagged(kMotionFlagSlipValid);
    out.gload_valid = record.flagged(kMotionFlagGLoadValid);
    out.fitted = record.flagged(kMotionFlagFitted);
    return true;
}

Record record_of(const Power& value, const Instant& at) {
    Record r = framed(Type::Power, at);
    put_u16(r.payload + 0, value.cell_mv);
    put_u16(r.payload + 2, clamp_u16(value.supply_warnings));
    put_u16(r.payload + 4, clamp_u16(value.implausible));
    put_u16(r.payload + 6, clamp_u16(value.charge_warnings));
    put_i16(r.payload + 8, value.die_dc);
    r.payload[10] = value.percent;
    r.payload[11] = static_cast<uint8_t>(value.level);
    r.payload[12] = static_cast<uint8_t>(value.charge);
    put_i16(r.payload + 13, value.trim_offset_mv);
    r.payload[15] = static_cast<uint8_t>(value.sample_offset_mv);
    set_flag(r.flags, kPowerFlagCharging, value.charging);
    set_flag(r.flags, kPowerFlagExternal, value.external_power);
    set_flag(r.flags, kPowerFlagValid, value.valid);
    set_flag(r.flags, kPowerFlagDieValid, value.die_valid);
    set_flag(r.flags, kPowerFlagCaution, value.caution);
    set_flag(r.flags, kPowerFlagTrimLearned, value.trim_learned);
    return r;
}

bool read(const Record& record, Power& out) {
    if (record.type != Type::Power) return false;
    out = Power{};
    out.cell_mv = get_u16(record.payload + 0);
    out.supply_warnings = get_u16(record.payload + 2);
    out.implausible = get_u16(record.payload + 4);
    out.charge_warnings = get_u16(record.payload + 6);
    out.die_dc = get_i16(record.payload + 8);
    out.percent = record.payload[10];
    out.level = static_cast<power::PowerLevel>(record.payload[11]);
    out.charge = static_cast<power::ChargeCondition>(record.payload[12]);
    out.trim_offset_mv = get_i16(record.payload + 13);
    out.sample_offset_mv = get_i8(record.payload + 15);
    out.charging = record.flagged(kPowerFlagCharging);
    out.external_power = record.flagged(kPowerFlagExternal);
    out.valid = record.flagged(kPowerFlagValid);
    out.die_valid = record.flagged(kPowerFlagDieValid);
    out.caution = record.flagged(kPowerFlagCaution);
    out.trim_learned = record.flagged(kPowerFlagTrimLearned);
    return true;
}

Record record_of(const Contact& value, const Instant& at) {
    Record r = framed(Type::Contact, at);
    put_u32(r.payload + 0, value.at_ms);
    put_u32(r.payload + 4, value.held_ms);
    r.payload[8] = static_cast<uint8_t>(value.contact);
    r.payload[9] = value.gesture;
    set_flag(r.flags, kContactFlagDown, value.down);
    return r;
}

bool read(const Record& record, Contact& out) {
    if (record.type != Type::Contact) return false;
    out = Contact{};
    out.at_ms = get_u32(record.payload + 0);
    out.held_ms = get_u32(record.payload + 4);
    out.contact = static_cast<events::Contact>(record.payload[8]);
    out.gesture = record.payload[9];
    out.down = record.flagged(kContactFlagDown);
    return true;
}

Record record_of(const Link& value, const Instant& at) {
    Record r = framed(Type::Link, at);
    put_u16(r.payload + 0, value.session);
    put_u16(r.payload + 2, value.payload_bytes);
    put_u16(r.payload + 4, value.frame_bytes);
    put_u16(r.payload + 6, value.holder);
    put_u16(r.payload + 8, clamp_u16(value.drops));
    r.payload[10] = static_cast<uint8_t>(value.action);
    r.payload[11] = static_cast<uint8_t>(value.endpoint);
    set_flag(r.flags, kLinkFlagClaimHeld, value.claim_held);
    return r;
}

bool read(const Record& record, Link& out) {
    if (record.type != Type::Link) return false;
    out = Link{};
    out.session = get_u16(record.payload + 0);
    out.payload_bytes = get_u16(record.payload + 2);
    out.frame_bytes = get_u16(record.payload + 4);
    out.holder = get_u16(record.payload + 6);
    out.drops = get_u16(record.payload + 8);
    out.action = static_cast<LinkAction>(record.payload[10]);
    out.endpoint = static_cast<events::Endpoint>(record.payload[11]);
    out.claim_held = record.flagged(kLinkFlagClaimHeld);
    return true;
}

Record record_of(const Switch& value, const Instant& at) {
    Record r = framed(Type::Switch, at);
    put_u32(r.payload + 0, value.to_hz);
    put_i32(r.payload + 4, value.margin_us);
    put_u16(r.payload + 8, value.took_us);
    put_u16(r.payload + 10, value.gap_us);
    r.payload[12] = static_cast<uint8_t>(value.kind);
    set_flag(r.flags, kSwitchFlagArmedAhead, value.armed_ahead);
    set_flag(r.flags, kSwitchFlagLate, value.late);
    return r;
}

bool read(const Record& record, Switch& out) {
    if (record.type != Type::Switch) return false;
    out = Switch{};
    out.to_hz = get_u32(record.payload + 0);
    out.margin_us = get_i32(record.payload + 4);
    out.took_us = get_u16(record.payload + 8);
    out.gap_us = get_u16(record.payload + 10);
    out.kind = static_cast<SwitchKind>(record.payload[12]);
    out.armed_ahead = record.flagged(kSwitchFlagArmedAhead);
    out.late = record.flagged(kSwitchFlagLate);
    return true;
}

}  // namespace skyblip::diag
