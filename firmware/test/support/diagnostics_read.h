// Harness, not a test: the decoders of what core/diagnostics encodes. The device never reads a
// record back.
#ifndef SKYBLIP_TEST_SUPPORT_DIAGNOSTICS_READ_H
#define SKYBLIP_TEST_SUPPORT_DIAGNOSTICS_READ_H

#include <cstdint>

#include "core/diagnostics/payload.h"
#include "protocols/common/little_endian.h"

namespace skyblip::diagnostics {

constexpr uint8_t kAdsbVersionMask = 0x07;
constexpr uint8_t kAdsbGvaShift = 3;
constexpr uint8_t kAdsbGvaMask = 0x03;

inline bool read(const Record& record, Gnss& out) {
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

inline bool read(const Record& record, Pps& out) {
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

inline bool read(const Record& record, radio::Entry& out) {
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

inline bool read(const Record& record, Barometer& out) {
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

inline bool read(const Record& record, Motion& out) {
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

inline bool read(const Record& record, Power& out) {
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

inline bool read(const Record& record, Contact& out) {
    if (record.type != Type::Contact) return false;
    out = Contact{};
    out.at_ms = wire::get_uint32(record.payload + 0);
    out.held_ms = wire::get_uint32(record.payload + 4);
    out.contact = static_cast<events::Contact>(record.payload[8]);
    out.gesture = record.payload[9];
    out.down = record.flagged(kContactFlagDown);
    return true;
}

inline bool read(const Record& record, Link& out) {
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

inline bool read(const Record& record, Switch& out) {
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

inline bool read(const Record& record, Adsb& out) {
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

inline bool read(const Record& record, Boot& out) {
    if (record.type != Type::Boot) return false;
    out = Boot{};
    out.capabilities = wire::get_uint32(record.payload + 0);
    out.firmware_build = wire::get_uint32(record.payload + 4);
    out.firmware_revision = wire::get_uint16(record.payload + 8);
    out.firmware_major = record.payload[10];
    out.firmware_minor = record.payload[11];
    out.reset = static_cast<power::ResetReason>(record.payload[12]);
    out.image_state = static_cast<dfu::ImageState>(record.payload[13]);
    out.profile = static_cast<Profile>(record.payload[14]);
    out.profile_recorded = record.flagged(kBootFlagProfileRecorded);
    return true;
}

inline bool read(const Record& record, Config& out) {
    if (record.type != Type::Config) return false;
    out = Config{};
    out.address = wire::get_uint32(record.payload + 0);
    out.battery_offset_mv = wire::get_int16(record.payload + 4);
    out.frequency_trim_e1_ppm = wire::get_int16(record.payload + 6);
    out.aircraft_type = record.payload[8];
    out.address_mapping_table = record.payload[9];
    out.settings_version = record.payload[11];
    out.tx_power_dbm = get_int8(record.payload + 12);
    out.pa_rated_dbm = get_int8(record.payload + 13);
    out.alarm_setting = record.payload[14];
    out.alarm_setting_recorded = record.flagged(kConfigFlagAlarmSettingRecorded);
    out.metric = record.flagged(kConfigFlagMetric);
    out.battery_trim_manual = record.flagged(kConfigFlagBatteryTrimManual);
    return true;
}

inline bool read(const Record& record, Dwell& out) {
    if (record.type != Type::Dwell) return false;
    out = Dwell{};
    out.frequency_hz = wire::get_uint32(record.payload + 0);
    out.start_ms = wire::get_uint16(record.payload + 4);
    out.end_ms = wire::get_uint16(record.payload + 6);
    out.phase_ms = wire::get_uint16(record.payload + 8);
    out.duty_permille = wire::get_uint16(record.payload + 10);
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

inline bool read(const Record& record, Flight& out) {
    if (record.type != Type::Flight) return false;
    out = Flight{};
    out.speed_mm_s = wire::get_int32(record.payload + 0);
    out.climb_mm_s = wire::get_int32(record.payload + 4);
    out.altitude_msl_m = wire::get_int16(record.payload + 8);
    out.hdop_e2 = wire::get_uint16(record.payload + 10);
    out.vdop_e2 = wire::get_uint16(record.payload + 12);
    out.declared = static_cast<flight::FlightState>(record.payload[14]);
    out.confirmed = static_cast<flight::FlightState>(record.payload[15]);
    out.fix_valid = record.flagged(kFlightFlagFixValid);
    out.rolling = record.flagged(kFlightFlagRolling);
    out.climb_valid = record.flagged(kFlightFlagClimbValid);
    out.tx_settled = record.flagged(kFlightFlagTxSettled);
    return true;
}

inline bool read(const Record& record, Traffic& out) {
    if (record.type != Type::Traffic) return false;
    out = Traffic{};
    out.address = wire::get_uint32(record.payload + 0);
    out.distance_m = wire::get_uint16(record.payload + 4);
    out.bearing_deg = wire::get_uint16(record.payload + 6);
    out.relative_altitude_m = wire::get_int16(record.payload + 8);
    out.closing_mps = wire::get_int16(record.payload + 10);
    out.alarm = static_cast<traffic::Level>(record.payload[12]);
    out.source = static_cast<model::Source>(record.payload[13]);
    out.rssi_dbm = get_int8(record.payload + 14);
    out.tracked = record.payload[15];
    out.assessed = record.flagged(kTrafficFlagAssessed);
    out.dismissed = record.flagged(kTrafficFlagDismissed);
    out.in_formation = record.flagged(kTrafficFlagInFormation);
    out.position_valid = record.flagged(kTrafficFlagPositionValid);
    out.relative_altitude_valid = record.flagged(kTrafficFlagRelativeAltitudeValid);
    return true;
}

inline bool read(const Record& record, Write& out) {
    if (record.type != Type::Write) return false;
    out = Write{};
    out.waited_ms = wire::get_uint32(record.payload + 0);
    out.phase_ms = wire::get_uint16(record.payload + 4);
    out.requests = wire::get_uint16(record.payload + 6);
    out.writes = wire::get_uint16(record.payload + 8);
    out.forced = wire::get_uint16(record.payload + 10);
    out.placement = static_cast<timing::DurableWriteVerdict>(record.payload[12]);
    out.kind = static_cast<power::DurableWrite>(record.payload[13]);
    out.pending = record.flagged(kWriteFlagPending);
    return true;
}

inline bool read(const Record& record, Screen& out) {
    if (record.type != Type::Screen) return false;
    out = Screen{};
    out.since_page_shown_ms = wire::get_uint32(record.payload + 0);
    out.page = record.payload[4];
    out.mode = record.payload[5];
    out.prompt = record.payload[6];
    out.alarm = static_cast<traffic::Level>(record.payload[7]);
    out.render_us = wire::get_uint32(record.payload + 8);
    out.present_us = wire::get_uint32(record.payload + 12);
    out.backlight = record.flagged(kScreenFlagBacklight);
    out.powered = record.flagged(kScreenFlagPowered);
    out.holding = record.flagged(kScreenFlagHolding);
    return true;
}

inline bool read(const Record& record, Loop& out) {
    if (record.type != Type::Loop) return false;
    out = Loop{};
    out.passes = wire::get_uint16(record.payload + 0);
    out.busy_ms = wire::get_uint16(record.payload + 2);
    out.worst_pass_us = wire::get_uint32(record.payload + 4);
    out.worst_pass_phase_ms = wire::get_uint16(record.payload + 8);
    out.worst_gap_ms = wire::get_uint16(record.payload + 10);
    out.worst_gap_phase_ms = wire::get_uint16(record.payload + 12);
    out.worst_service = static_cast<LoopService>(record.payload[14]);
    out.worst_tick_ms = record.payload[15];
    return true;
}

inline bool read(const Record& record, Gap& out) {
    if (record.type != Type::Gap) return false;
    out = Gap{};
    out.dropped = wire::get_uint32(record.payload + 0);
    out.span_ms = wire::get_uint32(record.payload + 4);
    out.total = wire::get_uint32(record.payload + 8);
    out.capacity = wire::get_uint16(record.payload + 12);
    return true;
}

inline bool read(const Record& record, End& out) {
    if (record.type != Type::End) return false;
    out = End{};
    out.records = wire::get_uint32(record.payload + 0);
    out.dropped = wire::get_uint32(record.payload + 4);
    return true;
}

inline bool read(const Record& record, Duty& out) {
    if (record.type != Type::Duty) return false;
    out = Duty{};
    out.panel_partial_refreshes = wire::get_uint16(record.payload + 0);
    out.panel_full_refreshes = wire::get_uint16(record.payload + 2);
    out.backlight_ms = wire::get_uint16(record.payload + 4);
    out.rx_armed_ms = wire::get_uint16(record.payload + 6);
    out.tx_keyed_ms = wire::get_uint16(record.payload + 8);
    out.ble_connected_ms = wire::get_uint16(record.payload + 10);
    out.annunciator_ms = wire::get_uint16(record.payload + 12);
    return true;
}

}  // namespace skyblip::diagnostics

#endif
