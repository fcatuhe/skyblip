#include "core/diagnostics/payload.h"
#include "protocols/common/little_endian.h"

namespace skyblip::diagnostics {

Record record_of(const Boot& value, const Instant& at) {
    Record record = framed(Type::Boot, at);
    wire::put_uint32(record.payload + boot_at::kCapabilities, value.capabilities);
    wire::put_uint32(record.payload + boot_at::kFirmwareBuild, value.firmware_build);
    wire::put_uint16(record.payload + boot_at::kFirmwareRevision, value.firmware_revision);
    record.payload[boot_at::kFirmwareMajor] = value.firmware_major;
    record.payload[boot_at::kFirmwareMinor] = value.firmware_minor;
    record.payload[boot_at::kReset] = static_cast<uint8_t>(value.reset);
    record.payload[boot_at::kImageState] = static_cast<uint8_t>(value.image_state);
    record.payload[boot_at::kProfile] = static_cast<uint8_t>(value.profile);
    set_flag(record.flags, kBootFlagProfileRecorded, value.profile_recorded);
    return record;
}

Record record_of(const Config& value, const Instant& at) {
    Record record = framed(Type::Config, at);
    wire::put_uint32(record.payload + config_at::kAddress, value.address);
    wire::put_int16(record.payload + config_at::kBatteryOffsetMv, value.battery_offset_mv);
    wire::put_int16(record.payload + config_at::kFrequencyTrimE1Ppm, value.frequency_trim_e1_ppm);
    record.payload[config_at::kAircraftType] = value.aircraft_type;
    record.payload[config_at::kAddressMappingTable] = value.address_mapping_table;
    record.payload[config_at::kSettingsVersion] = value.settings_version;
    put_int8(record.payload + config_at::kTxPowerDbm, value.tx_power_dbm);
    put_int8(record.payload + config_at::kPaRatedDbm, value.pa_rated_dbm);
    record.payload[config_at::kAlarmSetting] = value.alarm_setting;
    set_flag(record.flags, kConfigFlagAlarmSettingRecorded, value.alarm_setting_recorded);
    set_flag(record.flags, kConfigFlagMetric, value.metric);
    set_flag(record.flags, kConfigFlagBatteryTrimManual, value.battery_trim_manual);
    return record;
}

Record record_of(const Dwell& value, const Instant& at) {
    Record record = framed(Type::Dwell, at);
    wire::put_uint32(record.payload + dwell_at::kFrequencyHz, value.frequency_hz);
    wire::put_uint16(record.payload + dwell_at::kStartMs, value.start_ms);
    wire::put_uint16(record.payload + dwell_at::kEndMs, value.end_ms);
    wire::put_uint16(record.payload + dwell_at::kPhaseMs, value.phase_ms);
    wire::put_uint16(record.payload + dwell_at::kDutyPermille, value.duty_permille);
    record.payload[dwell_at::kState] = static_cast<uint8_t>(value.state);
    record.payload[dwell_at::kBand] = static_cast<uint8_t>(value.band);
    record.payload[dwell_at::kRefusal] = static_cast<uint8_t>(value.refusal);
    put_int8(record.payload + dwell_at::kNoiseDbm, value.noise_dbm);
    set_flag(record.flags, kDwellFlagTxAllowed, value.tx_allowed);
    set_flag(record.flags, kDwellFlagOwnTxDwell, value.own_tx_dwell);
    set_flag(record.flags, kDwellFlagListenOnly, value.listen_only);
    set_flag(record.flags, kDwellFlagArmed, value.armed);
    set_flag(record.flags, kDwellFlagBurstArmed, value.burst_armed);
    return record;
}

Record record_of(const Flight& value, const Instant& at) {
    Record record = framed(Type::Flight, at);
    wire::put_int32(record.payload + flight_at::kSpeedMmS, value.speed_mm_s);
    wire::put_int32(record.payload + flight_at::kClimbMmS, value.climb_mm_s);
    wire::put_int16(record.payload + flight_at::kAltitudeMslM, value.altitude_msl_m);
    wire::put_uint16(record.payload + flight_at::kHdopE2, value.hdop_e2);
    wire::put_uint16(record.payload + flight_at::kVdopE2, value.vdop_e2);
    record.payload[flight_at::kDeclared] = static_cast<uint8_t>(value.declared);
    record.payload[flight_at::kConfirmed] = static_cast<uint8_t>(value.confirmed);
    set_flag(record.flags, kFlightFlagFixValid, value.fix_valid);
    set_flag(record.flags, kFlightFlagRolling, value.rolling);
    set_flag(record.flags, kFlightFlagClimbValid, value.climb_valid);
    set_flag(record.flags, kFlightFlagTxSettled, value.tx_settled);
    return record;
}

Record record_of(const Traffic& value, const Instant& at) {
    Record record = framed(Type::Traffic, at);
    wire::put_uint32(record.payload + traffic_at::kAddress, value.address);
    wire::put_uint16(record.payload + traffic_at::kDistanceM, value.distance_m);
    wire::put_uint16(record.payload + traffic_at::kBearingDeg, value.bearing_deg);
    wire::put_int16(record.payload + traffic_at::kRelativeAltitudeM, value.relative_altitude_m);
    wire::put_int16(record.payload + traffic_at::kClosingMps, value.closing_mps);
    record.payload[traffic_at::kAlarm] = static_cast<uint8_t>(value.alarm);
    record.payload[traffic_at::kSource] = static_cast<uint8_t>(value.source);
    put_int8(record.payload + traffic_at::kRssiDbm, value.rssi_dbm);
    record.payload[traffic_at::kTracked] = value.tracked;
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
    wire::put_uint32(record.payload + write_at::kWaitedMs, value.waited_ms);
    wire::put_uint16(record.payload + write_at::kPhaseMs, value.phase_ms);
    wire::put_uint16(record.payload + write_at::kRequests, clamp_uint16(value.requests));
    wire::put_uint16(record.payload + write_at::kWrites, clamp_uint16(value.writes));
    wire::put_uint16(record.payload + write_at::kForced, clamp_uint16(value.forced));
    record.payload[write_at::kPlacement] = static_cast<uint8_t>(value.placement);
    record.payload[write_at::kKind] = static_cast<uint8_t>(value.kind);
    set_flag(record.flags, kWriteFlagPending, value.pending);
    return record;
}

Record record_of(const Screen& value, const Instant& at) {
    Record record = framed(Type::Screen, at);
    wire::put_uint32(record.payload + screen_at::kSincePageShownMs, value.since_page_shown_ms);
    record.payload[screen_at::kPage] = value.page;
    record.payload[screen_at::kMode] = value.mode;
    record.payload[screen_at::kPrompt] = value.prompt;
    record.payload[screen_at::kAlarm] = static_cast<uint8_t>(value.alarm);
    wire::put_uint32(record.payload + screen_at::kRenderUs, value.render_us);
    wire::put_uint32(record.payload + screen_at::kPresentUs, value.present_us);
    set_flag(record.flags, kScreenFlagBacklight, value.backlight);
    set_flag(record.flags, kScreenFlagPowered, value.powered);
    set_flag(record.flags, kScreenFlagHolding, value.holding);
    return record;
}

Record record_of(const Loop& value, const Instant& at) {
    Record record = framed(Type::Loop, at);
    wire::put_uint16(record.payload + loop_at::kPasses, value.passes);
    wire::put_uint16(record.payload + loop_at::kBusyMs, value.busy_ms);
    wire::put_uint32(record.payload + loop_at::kWorstPassUs, value.worst_pass_us);
    wire::put_uint16(record.payload + loop_at::kWorstPassPhaseMs, value.worst_pass_phase_ms);
    wire::put_uint16(record.payload + loop_at::kWorstGapMs, value.worst_gap_ms);
    wire::put_uint16(record.payload + loop_at::kWorstGapPhaseMs, value.worst_gap_phase_ms);
    record.payload[loop_at::kWorstService] = static_cast<uint8_t>(value.worst_service);
    record.payload[loop_at::kWorstTickMs] = value.worst_tick_ms;
    return record;
}

Record record_of(const Gap& value, const Instant& at) {
    Record record = framed(Type::Gap, at);
    wire::put_uint32(record.payload + gap_at::kDropped, value.dropped);
    wire::put_uint32(record.payload + gap_at::kSpanMs, value.span_ms);
    wire::put_uint32(record.payload + gap_at::kTotal, value.total);
    wire::put_uint16(record.payload + gap_at::kCapacity, value.capacity);
    return record;
}

Record record_of(const End& value, const Instant& at) {
    Record record = framed(Type::End, at);
    wire::put_uint32(record.payload + end_at::kRecords, value.records);
    wire::put_uint32(record.payload + end_at::kDropped, value.dropped);
    return record;
}

Record record_of(const Duty& value, const Instant& at) {
    Record record = framed(Type::Duty, at);
    // INFO: fc 21sep26 the low half, not clamp_uint16: readers subtract, and a clamp loses deltas
    wire::put_uint16(record.payload + duty_at::kPanelPartialRefreshes,
                     static_cast<uint16_t>(value.panel_partial_refreshes));
    wire::put_uint16(record.payload + duty_at::kPanelFullRefreshes,
                     static_cast<uint16_t>(value.panel_full_refreshes));
    wire::put_uint16(record.payload + duty_at::kBacklightMs,
                     static_cast<uint16_t>(value.backlight_ms));
    wire::put_uint16(record.payload + duty_at::kRxArmedMs,
                     static_cast<uint16_t>(value.rx_armed_ms));
    wire::put_uint16(record.payload + duty_at::kTxKeyedMs,
                     static_cast<uint16_t>(value.tx_keyed_ms));
    wire::put_uint16(record.payload + duty_at::kBleConnectedMs,
                     static_cast<uint16_t>(value.ble_connected_ms));
    wire::put_uint16(record.payload + duty_at::kAnnunciatorMs,
                     static_cast<uint16_t>(value.annunciator_ms));
    return record;
}

Record record_of(const Reception& value, const Instant& at) {
    Record record = framed(Type::Reception, at);
    wire::put_uint16(record.payload + reception_at::kCrc, static_cast<uint16_t>(value.crc));
    wire::put_uint16(record.payload + reception_at::kUnsupported,
                     static_cast<uint16_t>(value.unsupported));
    wire::put_uint16(record.payload + reception_at::kFolded, static_cast<uint16_t>(value.folded));
    wire::put_uint16(record.payload + reception_at::kPositions,
                     static_cast<uint16_t>(value.positions));
    wire::put_uint16(record.payload + reception_at::kCaptureRefused,
                     static_cast<uint16_t>(value.capture_refused));
    record.payload[reception_at::kAircraft] = clamped<uint8_t>(value.aircraft);
    record.payload[reception_at::kTracked] = clamped<uint8_t>(value.tracked);
    record.payload[reception_at::kReference] = static_cast<uint8_t>(value.reference);
    set_flag(record.flags, kReceptionFlagFixValid, value.fix_valid);
    return record;
}

}  // namespace skyblip::diagnostics
