#include "core/diagnostics/payload.h"
#include "core/flight/extrapolate.h"
#include "protocols/common/little_endian.h"

namespace skyblip::diagnostics {

namespace {

constexpr uint32_t kAdsbAddressMask = 0xFFFFFF;
constexpr uint8_t kAdsbVersionMask = 0x07;
constexpr uint8_t kAdsbGvaShift = 3;
constexpr uint8_t kAdsbGvaMask = 0x03;

}  // namespace

Record record_of(const Gnss& value, const Instant& at) {
    Record record = framed(Type::Gnss, at);
    wire::put_uint16(record.payload + 0, value.solution_phase_ms);
    wire::put_uint16(record.payload + 2, value.residual_m);
    wire::put_uint16(record.payload + 4, value.hdop_e2);
    wire::put_uint16(record.payload + 6, value.vdop_e2);
    wire::put_uint16(record.payload + 8, clamp_uint16(value.stage_s));
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
    out.solution_phase_ms = wire::get_uint16(record.payload + 0);
    out.residual_m = wire::get_uint16(record.payload + 2);
    out.hdop_e2 = wire::get_uint16(record.payload + 4);
    out.vdop_e2 = wire::get_uint16(record.payload + 6);
    out.stage_s = wire::get_uint16(record.payload + 8);
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
    wire::put_uint32(record.payload + 0, value.interval_us);
    wire::put_int32(record.payload + 4, value.error_us);
    wire::put_uint32(record.payload + 8, value.samples);
    wire::put_uint16(record.payload + 12, clamp_uint16(value.holdover_events));
    wire::put_uint16(record.payload + 14, value.since_edge_ms);
    set_flag(record.flags, kPpsFlagLocked, value.locked);
    set_flag(record.flags, kPpsFlagUtcValid, value.utc_valid);
    return record;
}

bool read(const Record& record, Pps& out) {
    if (record.type != Type::Pps) return false;
    out = Pps{};
    out.interval_us = wire::get_uint32(record.payload + 0);
    out.error_us = wire::get_int32(record.payload + 4);
    out.samples = wire::get_uint32(record.payload + 8);
    out.holdover_events = wire::get_uint16(record.payload + 12);
    out.since_edge_ms = wire::get_uint16(record.payload + 14);
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
    wire::put_uint32(record.payload + 0, value.address);
    wire::put_uint16(record.payload + 4, value.tx_keyed_us);
    wire::put_uint16(record.payload + 6, value.tx_span_us);
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
    set_flag(record.flags, kBurstFlagStatus, value.status);
    return record;
}

bool read(const Record& record, radio::Entry& out) {
    if (record.type != Type::Burst) return false;
    out = radio::Entry{};
    out.at_s = record.at_s;
    out.into_ms = record.into_ms;
    out.phase_valid = record.phase_valid();
    out.utc_dated = record.utc_dated();
    out.address = wire::get_uint32(record.payload + 0);
    out.tx_keyed_us = wire::get_uint16(record.payload + 4);
    out.tx_span_us = wire::get_uint16(record.payload + 6);
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
    out.status = record.flagged(kBurstFlagStatus);
    return true;
}

Record record_of(const Barometer& value, const Instant& at) {
    Record record = framed(Type::Barometer, at);
    wire::put_uint32(record.payload + 0, value.pressure_mpa);
    wire::put_int32(record.payload + 4, value.altitude_mm);
    wire::put_int32(record.payload + 8, value.climb_mm_s);
    wire::put_int16(record.payload + 12, value.temperature_dc);
    set_flag(record.flags, kBarometerFlagActive, value.active);
    set_flag(record.flags, kBarometerFlagTemperatureValid, value.temperature_valid);
    set_flag(record.flags, kBarometerFlagClimbAdopted, value.climb_adopted);
    return record;
}

bool read(const Record& record, Barometer& out) {
    if (record.type != Type::Barometer) return false;
    out = Barometer{};
    out.pressure_mpa = wire::get_uint32(record.payload + 0);
    out.altitude_mm = wire::get_int32(record.payload + 4);
    out.climb_mm_s = wire::get_int32(record.payload + 8);
    out.temperature_dc = wire::get_int16(record.payload + 12);
    out.active = record.flagged(kBarometerFlagActive);
    out.temperature_valid = record.flagged(kBarometerFlagTemperatureValid);
    out.climb_adopted = record.flagged(kBarometerFlagClimbAdopted);
    return true;
}

Record record_of(const Motion& value, const Instant& at) {
    Record record = framed(Type::Motion, at);
    wire::put_int16(record.payload + 0, value.slip_mg);
    wire::put_int16(record.payload + 2, value.normal_mg);
    wire::put_int16(record.payload + 4, value.lateral_mg);
    wire::put_int16(record.payload + 6, value.longitudinal_mg);
    wire::put_int16(record.payload + 8, value.most_normal_mg);
    wire::put_int16(record.payload + 10, value.least_normal_mg);
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
    out.slip_mg = wire::get_int16(record.payload + 0);
    out.normal_mg = wire::get_int16(record.payload + 2);
    out.lateral_mg = wire::get_int16(record.payload + 4);
    out.longitudinal_mg = wire::get_int16(record.payload + 6);
    out.most_normal_mg = wire::get_int16(record.payload + 8);
    out.least_normal_mg = wire::get_int16(record.payload + 10);
    out.imu_error = record.payload[12];
    out.sensor_error = record.payload[13];
    out.slip_valid = record.flagged(kMotionFlagSlipValid);
    out.gload_valid = record.flagged(kMotionFlagGLoadValid);
    out.fitted = record.flagged(kMotionFlagFitted);
    return true;
}

Record record_of(const Power& value, const Instant& at) {
    Record record = framed(Type::Power, at);
    wire::put_uint16(record.payload + 0, value.cell_mv);
    wire::put_uint16(record.payload + 2, clamp_uint16(value.supply_warnings));
    wire::put_uint16(record.payload + 4, clamp_uint16(value.implausible));
    wire::put_uint16(record.payload + 6, clamp_uint16(value.charge_warnings));
    wire::put_int16(record.payload + 8, value.die_dc);
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
    out.cell_mv = wire::get_uint16(record.payload + 0);
    out.supply_warnings = wire::get_uint16(record.payload + 2);
    out.implausible = wire::get_uint16(record.payload + 4);
    out.charge_warnings = wire::get_uint16(record.payload + 6);
    out.die_dc = wire::get_int16(record.payload + 8);
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
    wire::put_uint32(record.payload + 0, value.at_ms);
    wire::put_uint32(record.payload + 4, value.held_ms);
    record.payload[8] = static_cast<uint8_t>(value.contact);
    record.payload[9] = value.gesture;
    set_flag(record.flags, kContactFlagDown, value.down);
    return record;
}

bool read(const Record& record, Contact& out) {
    if (record.type != Type::Contact) return false;
    out = Contact{};
    out.at_ms = wire::get_uint32(record.payload + 0);
    out.held_ms = wire::get_uint32(record.payload + 4);
    out.contact = static_cast<events::Contact>(record.payload[8]);
    out.gesture = record.payload[9];
    out.down = record.flagged(kContactFlagDown);
    return true;
}

Record record_of(const Link& value, const Instant& at) {
    Record record = framed(Type::Link, at);
    wire::put_uint16(record.payload + 0, value.link_session);
    wire::put_uint16(record.payload + 2, value.payload_bytes);
    wire::put_uint16(record.payload + 4, value.frame_bytes);
    wire::put_uint16(record.payload + 6, value.holder);
    wire::put_uint16(record.payload + 8, clamp_uint16(value.drops));
    record.payload[10] = static_cast<uint8_t>(value.action);
    record.payload[11] = static_cast<uint8_t>(value.endpoint);
    record.payload[12] = static_cast<uint8_t>(value.mtu_exchange_started_by);
    set_flag(record.flags, kLinkFlagClaimHeld, value.claim_held);
    return record;
}

bool read(const Record& record, Link& out) {
    if (record.type != Type::Link) return false;
    out = Link{};
    out.link_session = wire::get_uint16(record.payload + 0);
    out.payload_bytes = wire::get_uint16(record.payload + 2);
    out.frame_bytes = wire::get_uint16(record.payload + 4);
    out.holder = wire::get_uint16(record.payload + 6);
    out.drops = wire::get_uint16(record.payload + 8);
    out.action = static_cast<LinkAction>(record.payload[10]);
    out.endpoint = static_cast<events::Endpoint>(record.payload[11]);
    out.mtu_exchange_started_by = static_cast<events::MtuExchangeStarter>(record.payload[12]);
    out.claim_held = record.flagged(kLinkFlagClaimHeld);
    return true;
}

Record record_of(const Switch& value, const Instant& at) {
    Record record = framed(Type::Switch, at);
    wire::put_uint32(record.payload + 0, value.to_hz);
    wire::put_int32(record.payload + 4, value.margin_us);
    wire::put_uint16(record.payload + 8, value.took_us);
    wire::put_uint16(record.payload + 10, value.gap_us);
    record.payload[12] = static_cast<uint8_t>(value.change);
    set_flag(record.flags, kSwitchFlagArmedAhead, value.armed_ahead);
    set_flag(record.flags, kSwitchFlagLate, value.late);
    return record;
}

bool read(const Record& record, Switch& out) {
    if (record.type != Type::Switch) return false;
    out = Switch{};
    out.to_hz = wire::get_uint32(record.payload + 0);
    out.margin_us = wire::get_int32(record.payload + 4);
    out.took_us = wire::get_uint16(record.payload + 8);
    out.gap_us = wire::get_uint16(record.payload + 10);
    out.change = static_cast<SwitchKind>(record.payload[12]);
    out.armed_ahead = record.flagged(kSwitchFlagArmedAhead);
    out.late = record.flagged(kSwitchFlagLate);
    return true;
}

Adsb adsb_of(const model::Ownship& own, const model::AircraftObservation& target, uint32_t now_ms) {
    Adsb value{};
    value.address = target.address;
    value.adsb_version = target.adsb_version;
    value.adsb_version_valid = target.adsb_version_valid;
    value.gva = target.gva;
    value.gva_valid = target.gva_valid;
    value.pressure_altitude_valid = target.pressure_altitude_valid;
    if (target.pressure_altitude_valid) value.pressure_altitude_ft = target.pressure_altitude_ft;
    value.height_difference_valid = target.height_difference_valid;
    if (target.height_difference_valid) {
        value.height_difference_ft = target.height_difference_ft;
        const int32_t since_position_ms = static_cast<int32_t>(now_ms - target.at_ms);
        value.height_difference_age_ms =
            clamp_int32(static_cast<int64_t>(target.height_difference_age_ms) + since_position_ms);
    }
    value.gnss_relative_altitude_valid =
        flight::gnss_relative_altitude_m(own, target, value.gnss_relative_altitude_m);
    value.pressure_relative_altitude_valid =
        flight::pressure_relative_altitude_m(own, target, value.pressure_relative_altitude_m);
    return value;
}

Record record_of(const Adsb& value, const Instant& at) {
    Record record = framed(Type::Adsb, at);
    const uint32_t address = value.address & kAdsbAddressMask;
    record.payload[0] = static_cast<uint8_t>(address);
    record.payload[1] = static_cast<uint8_t>(address >> 8);
    record.payload[2] = static_cast<uint8_t>(address >> 16);
    record.payload[3] = static_cast<uint8_t>((value.adsb_version & kAdsbVersionMask) |
                                             ((value.gva & kAdsbGvaMask) << kAdsbGvaShift));
    wire::put_int32(record.payload + 4, value.pressure_altitude_ft);
    wire::put_int16(record.payload + 8, clamp_int16(value.height_difference_ft));
    wire::put_int16(record.payload + 10, clamp_int16(value.height_difference_age_ms));
    wire::put_int16(record.payload + 12, clamp_int16(value.gnss_relative_altitude_m));
    wire::put_int16(record.payload + 14, clamp_int16(value.pressure_relative_altitude_m));
    set_flag(record.flags, kAdsbFlagVersionValid, value.adsb_version_valid);
    set_flag(record.flags, kAdsbFlagGvaValid, value.gva_valid);
    set_flag(record.flags, kAdsbFlagPressureAltitudeValid, value.pressure_altitude_valid);
    set_flag(record.flags, kAdsbFlagHeightDifferenceValid, value.height_difference_valid);
    set_flag(record.flags, kAdsbFlagGnssRelativeAltitudeValid, value.gnss_relative_altitude_valid);
    set_flag(record.flags, kAdsbFlagPressureRelativeAltitudeValid,
             value.pressure_relative_altitude_valid);
    return record;
}

bool read(const Record& record, Adsb& out) {
    if (record.type != Type::Adsb) return false;
    out = Adsb{};
    out.address = static_cast<uint32_t>(record.payload[0]) |
                  (static_cast<uint32_t>(record.payload[1]) << 8) |
                  (static_cast<uint32_t>(record.payload[2]) << 16);
    out.adsb_version = record.payload[3] & kAdsbVersionMask;
    out.gva = (record.payload[3] >> kAdsbGvaShift) & kAdsbGvaMask;
    out.pressure_altitude_ft = wire::get_int32(record.payload + 4);
    out.height_difference_ft = wire::get_int16(record.payload + 8);
    out.height_difference_age_ms = wire::get_int16(record.payload + 10);
    out.gnss_relative_altitude_m = wire::get_int16(record.payload + 12);
    out.pressure_relative_altitude_m = wire::get_int16(record.payload + 14);
    out.adsb_version_valid = record.flagged(kAdsbFlagVersionValid);
    out.gva_valid = record.flagged(kAdsbFlagGvaValid);
    out.pressure_altitude_valid = record.flagged(kAdsbFlagPressureAltitudeValid);
    out.height_difference_valid = record.flagged(kAdsbFlagHeightDifferenceValid);
    out.gnss_relative_altitude_valid = record.flagged(kAdsbFlagGnssRelativeAltitudeValid);
    out.pressure_relative_altitude_valid = record.flagged(kAdsbFlagPressureRelativeAltitudeValid);
    return true;
}

}  // namespace skyblip::diagnostics
