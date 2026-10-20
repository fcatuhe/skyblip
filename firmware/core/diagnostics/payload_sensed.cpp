#include "core/diagnostics/payload.h"

namespace skyblip::diagnostics {

Record record_of(const Gnss& value, const Instant& at) {
    Record record = framed(Type::Gnss, at);
    put_uint16(record.payload + 0, value.solution_phase_ms);
    put_uint16(record.payload + 2, value.residual_m);
    put_uint16(record.payload + 4, value.hdop_e2);
    put_uint16(record.payload + 6, value.vdop_e2);
    put_uint16(record.payload + 8, clamp_uint16(value.stage_s));
    record.payload[10] = value.satellites;
    record.payload[11] = value.satellites_in_view;
    record.payload[12] = value.fix_mode;
    record.payload[13] = static_cast<uint8_t>(value.reject);
    record.payload[14] = static_cast<uint8_t>(value.stage);
    set_flag(record.flags, kGnssFlagFixValid, value.fix_valid);
    set_flag(record.flags, kGnssFlagResidualValid, value.residual_valid);
    set_flag(record.flags, kGnssFlagPpsLocked, value.pps_locked);
    set_flag(record.flags, kGnssFlagGeoidMeasured, value.geoid_measured);
    set_flag(record.flags, kGnssFlagTxSettled, value.tx_settled);
    set_flag(record.flags, kGnssFlagLevelsFresh, value.levels_fresh);
    return record;
}

bool read(const Record& record, Gnss& out) {
    if (record.type != Type::Gnss) return false;
    out = Gnss{};
    out.solution_phase_ms = get_uint16(record.payload + 0);
    out.residual_m = get_uint16(record.payload + 2);
    out.hdop_e2 = get_uint16(record.payload + 4);
    out.vdop_e2 = get_uint16(record.payload + 6);
    out.stage_s = get_uint16(record.payload + 8);
    out.satellites = record.payload[10];
    out.satellites_in_view = record.payload[11];
    out.fix_mode = record.payload[12];
    out.reject = static_cast<gnss::FixReject>(record.payload[13]);
    out.stage = static_cast<gnss::Stage>(record.payload[14]);
    out.fix_valid = record.flagged(kGnssFlagFixValid);
    out.residual_valid = record.flagged(kGnssFlagResidualValid);
    out.pps_locked = record.flagged(kGnssFlagPpsLocked);
    out.geoid_measured = record.flagged(kGnssFlagGeoidMeasured);
    out.tx_settled = record.flagged(kGnssFlagTxSettled);
    out.levels_fresh = record.flagged(kGnssFlagLevelsFresh);
    return true;
}

Record record_of(const Pps& value, const Instant& at) {
    Record record = framed(Type::Pps, at);
    put_uint32(record.payload + 0, value.interval_us);
    put_int32(record.payload + 4, value.error_us);
    put_uint32(record.payload + 8, value.samples);
    put_uint16(record.payload + 12, clamp_uint16(value.holdover_events));
    put_uint16(record.payload + 14, value.since_edge_ms);
    set_flag(record.flags, kPpsFlagLocked, value.locked);
    set_flag(record.flags, kPpsFlagUtcValid, value.utc_valid);
    return record;
}

bool read(const Record& record, Pps& out) {
    if (record.type != Type::Pps) return false;
    out = Pps{};
    out.interval_us = get_uint32(record.payload + 0);
    out.error_us = get_int32(record.payload + 4);
    out.samples = get_uint32(record.payload + 8);
    out.holdover_events = get_uint16(record.payload + 12);
    out.since_edge_ms = get_uint16(record.payload + 14);
    out.locked = record.flagged(kPpsFlagLocked);
    out.utc_valid = record.flagged(kPpsFlagUtcValid);
    return true;
}

Record record_of(const radio::Entry& value) {
    Instant at{};
    at.at_s = value.at_s;
    at.into_ms = value.into_ms;
    at.phase_valid = value.phase_valid;
    at.utc_dated = value.utc_dated;
    Record record = framed(Type::Burst, at);
    put_uint32(record.payload + 0, value.address);
    put_uint16(record.payload + 4, value.tx_keyed_us);
    put_uint16(record.payload + 6, value.tx_span_us);
    record.payload[8] = static_cast<uint8_t>(value.verdict);
    record.payload[9] = static_cast<uint8_t>(value.source);
    record.payload[10] = static_cast<uint8_t>(value.band);
    record.payload[11] = value.channel;
    record.payload[12] = value.bytes;
    put_int8(record.payload + 13, value.rssi_dbm);
    put_int8(record.payload + 14, value.key_offset_s);
    put_int8(record.payload + 15, stage_margin_steps(value.tx_stage_margin_us));
    set_flag(record.flags, kBurstFlagAddressValid, value.address_valid);
    set_flag(record.flags, kBurstFlagRssiValid, value.rssi_valid);
    set_flag(record.flags, kBurstFlagAirborne, value.airborne);
    set_flag(record.flags, kBurstFlagTxSpanValid, value.tx_span_valid);
    set_flag(record.flags, kBurstFlagCallsign, value.callsign);
    return record;
}

bool read(const Record& record, radio::Entry& out) {
    if (record.type != Type::Burst) return false;
    out = radio::Entry{};
    out.at_s = record.at_s;
    out.into_ms = record.into_ms;
    out.phase_valid = record.phase_valid();
    out.utc_dated = record.utc_dated();
    out.address = get_uint32(record.payload + 0);
    out.tx_keyed_us = get_uint16(record.payload + 4);
    out.tx_span_us = get_uint16(record.payload + 6);
    out.verdict = static_cast<radio::Verdict>(record.payload[8]);
    out.source = static_cast<model::Source>(record.payload[9]);
    out.band = static_cast<model::Band>(record.payload[10]);
    out.channel = record.payload[11];
    out.bytes = record.payload[12];
    out.rssi_dbm = get_int8(record.payload + 13);
    out.key_offset_s = get_int8(record.payload + 14);
    out.tx_stage_margin_us =
        static_cast<int16_t>(get_int8(record.payload + 15) * kStageMarginStepUs);
    out.address_valid = record.flagged(kBurstFlagAddressValid);
    out.rssi_valid = record.flagged(kBurstFlagRssiValid);
    out.airborne = record.flagged(kBurstFlagAirborne);
    out.tx_span_valid = record.flagged(kBurstFlagTxSpanValid);
    out.callsign = record.flagged(kBurstFlagCallsign);
    return true;
}

Record record_of(const Barometer& value, const Instant& at) {
    Record record = framed(Type::Barometer, at);
    put_uint32(record.payload + 0, value.pressure_mpa);
    put_int32(record.payload + 4, value.altitude_mm);
    put_int32(record.payload + 8, value.climb_mm_s);
    put_int16(record.payload + 12, value.temperature_dc);
    set_flag(record.flags, kBarometerFlagActive, value.active);
    set_flag(record.flags, kBarometerFlagTemperatureValid, value.temperature_valid);
    set_flag(record.flags, kBarometerFlagClimbAdopted, value.climb_adopted);
    return record;
}

bool read(const Record& record, Barometer& out) {
    if (record.type != Type::Barometer) return false;
    out = Barometer{};
    out.pressure_mpa = get_uint32(record.payload + 0);
    out.altitude_mm = get_int32(record.payload + 4);
    out.climb_mm_s = get_int32(record.payload + 8);
    out.temperature_dc = get_int16(record.payload + 12);
    out.active = record.flagged(kBarometerFlagActive);
    out.temperature_valid = record.flagged(kBarometerFlagTemperatureValid);
    out.climb_adopted = record.flagged(kBarometerFlagClimbAdopted);
    return true;
}

Record record_of(const Motion& value, const Instant& at) {
    Record record = framed(Type::Motion, at);
    put_int16(record.payload + 0, value.slip_mg);
    put_int16(record.payload + 2, value.normal_mg);
    put_int16(record.payload + 4, value.lateral_mg);
    put_int16(record.payload + 6, value.longitudinal_mg);
    put_int16(record.payload + 8, value.most_normal_mg);
    put_int16(record.payload + 10, value.least_normal_mg);
    record.payload[12] = value.imu_error;
    record.payload[13] = value.sensor_error;
    set_flag(record.flags, kMotionFlagSlipValid, value.slip_valid);
    set_flag(record.flags, kMotionFlagGLoadValid, value.gload_valid);
    set_flag(record.flags, kMotionFlagFitted, value.fitted);
    return record;
}

bool read(const Record& record, Motion& out) {
    if (record.type != Type::Motion) return false;
    out = Motion{};
    out.slip_mg = get_int16(record.payload + 0);
    out.normal_mg = get_int16(record.payload + 2);
    out.lateral_mg = get_int16(record.payload + 4);
    out.longitudinal_mg = get_int16(record.payload + 6);
    out.most_normal_mg = get_int16(record.payload + 8);
    out.least_normal_mg = get_int16(record.payload + 10);
    out.imu_error = record.payload[12];
    out.sensor_error = record.payload[13];
    out.slip_valid = record.flagged(kMotionFlagSlipValid);
    out.gload_valid = record.flagged(kMotionFlagGLoadValid);
    out.fitted = record.flagged(kMotionFlagFitted);
    return true;
}

Record record_of(const Power& value, const Instant& at) {
    Record record = framed(Type::Power, at);
    put_uint16(record.payload + 0, value.cell_mv);
    put_uint16(record.payload + 2, clamp_uint16(value.supply_warnings));
    put_uint16(record.payload + 4, clamp_uint16(value.implausible));
    put_uint16(record.payload + 6, clamp_uint16(value.charge_warnings));
    put_int16(record.payload + 8, value.die_dc);
    record.payload[10] = value.percent;
    record.payload[11] = static_cast<uint8_t>(value.power_level);
    record.payload[12] = static_cast<uint8_t>(value.charge);
    record.payload[15] = static_cast<uint8_t>(value.sample_offset_mv);
    set_flag(record.flags, kPowerFlagCharging, value.charging);
    set_flag(record.flags, kPowerFlagExternalPower, value.external_power);
    set_flag(record.flags, kPowerFlagBatteryValid, value.battery_valid);
    set_flag(record.flags, kPowerFlagDieValid, value.die_valid);
    return record;
}

bool read(const Record& record, Power& out) {
    if (record.type != Type::Power) return false;
    out = Power{};
    out.cell_mv = get_uint16(record.payload + 0);
    out.supply_warnings = get_uint16(record.payload + 2);
    out.implausible = get_uint16(record.payload + 4);
    out.charge_warnings = get_uint16(record.payload + 6);
    out.die_dc = get_int16(record.payload + 8);
    out.percent = record.payload[10];
    out.power_level = static_cast<power::PowerLevel>(record.payload[11]);
    out.charge = static_cast<power::ChargeCondition>(record.payload[12]);
    out.sample_offset_mv = get_int8(record.payload + 15);
    out.charging = record.flagged(kPowerFlagCharging);
    out.external_power = record.flagged(kPowerFlagExternalPower);
    out.battery_valid = record.flagged(kPowerFlagBatteryValid);
    out.die_valid = record.flagged(kPowerFlagDieValid);
    return true;
}

Record record_of(const Contact& value, const Instant& at) {
    Record record = framed(Type::Contact, at);
    put_uint32(record.payload + 0, value.at_ms);
    put_uint32(record.payload + 4, value.held_ms);
    record.payload[8] = static_cast<uint8_t>(value.contact);
    record.payload[9] = value.gesture;
    set_flag(record.flags, kContactFlagDown, value.down);
    return record;
}

bool read(const Record& record, Contact& out) {
    if (record.type != Type::Contact) return false;
    out = Contact{};
    out.at_ms = get_uint32(record.payload + 0);
    out.held_ms = get_uint32(record.payload + 4);
    out.contact = static_cast<events::Contact>(record.payload[8]);
    out.gesture = record.payload[9];
    out.down = record.flagged(kContactFlagDown);
    return true;
}

Record record_of(const Link& value, const Instant& at) {
    Record record = framed(Type::Link, at);
    put_uint16(record.payload + 0, value.link_session);
    put_uint16(record.payload + 2, value.payload_bytes);
    put_uint16(record.payload + 4, value.frame_bytes);
    put_uint16(record.payload + 6, value.holder);
    put_uint16(record.payload + 8, clamp_uint16(value.drops));
    record.payload[10] = static_cast<uint8_t>(value.action);
    record.payload[11] = static_cast<uint8_t>(value.endpoint);
    set_flag(record.flags, kLinkFlagClaimHeld, value.claim_held);
    return record;
}

bool read(const Record& record, Link& out) {
    if (record.type != Type::Link) return false;
    out = Link{};
    out.link_session = get_uint16(record.payload + 0);
    out.payload_bytes = get_uint16(record.payload + 2);
    out.frame_bytes = get_uint16(record.payload + 4);
    out.holder = get_uint16(record.payload + 6);
    out.drops = get_uint16(record.payload + 8);
    out.action = static_cast<LinkAction>(record.payload[10]);
    out.endpoint = static_cast<events::Endpoint>(record.payload[11]);
    out.claim_held = record.flagged(kLinkFlagClaimHeld);
    return true;
}

Record record_of(const Switch& value, const Instant& at) {
    Record record = framed(Type::Switch, at);
    put_uint32(record.payload + 0, value.to_hz);
    put_int32(record.payload + 4, value.margin_us);
    put_uint16(record.payload + 8, value.took_us);
    put_uint16(record.payload + 10, value.gap_us);
    record.payload[12] = static_cast<uint8_t>(value.change);
    set_flag(record.flags, kSwitchFlagArmedAhead, value.armed_ahead);
    set_flag(record.flags, kSwitchFlagLate, value.late);
    return record;
}

bool read(const Record& record, Switch& out) {
    if (record.type != Type::Switch) return false;
    out = Switch{};
    out.to_hz = get_uint32(record.payload + 0);
    out.margin_us = get_int32(record.payload + 4);
    out.took_us = get_uint16(record.payload + 8);
    out.gap_us = get_uint16(record.payload + 10);
    out.change = static_cast<SwitchKind>(record.payload[12]);
    out.armed_ahead = record.flagged(kSwitchFlagArmedAhead);
    out.late = record.flagged(kSwitchFlagLate);
    return true;
}

}  // namespace skyblip::diagnostics
