// Harness, not a test: the decoders of what core/diagnostics encodes. The device never reads a
// record back.
#ifndef SKYBLIP_TEST_SUPPORT_DIAGNOSTICS_READ_H
#define SKYBLIP_TEST_SUPPORT_DIAGNOSTICS_READ_H

#include <cstdint>

#include "core/diagnostics/payload.h"
#include "protocols/common/little_endian.h"

namespace skyblip::diagnostics {

inline bool read(const Record& record, Gnss& out) {
    if (record.type != Type::Gnss) return false;
    out = Gnss{};
    out.solution_phase_ms = wire::get_uint16(record.payload + gnss_at::kSolutionPhaseMs);
    out.residual_m = wire::get_uint16(record.payload + gnss_at::kResidualM);
    out.hdop_e2 = wire::get_uint16(record.payload + gnss_at::kHdopE2);
    out.vdop_e2 = wire::get_uint16(record.payload + gnss_at::kVdopE2);
    out.stage_s = wire::get_uint16(record.payload + gnss_at::kStageS);
    out.satellites = record.payload[gnss_at::kSatellites];
    out.satellites_in_view = record.payload[gnss_at::kSatellitesInView];
    out.fix_mode = record.payload[gnss_at::kFixMode];
    out.reject = static_cast<gnss::FixReject>(record.payload[gnss_at::kReject]);
    out.stage = static_cast<gnss::Stage>(record.payload[gnss_at::kStage]);
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
    out.interval_us = wire::get_uint32(record.payload + pps_at::kIntervalUs);
    out.error_us = wire::get_int32(record.payload + pps_at::kErrorUs);
    out.samples = wire::get_uint32(record.payload + pps_at::kSamples);
    out.holdover_events = wire::get_uint16(record.payload + pps_at::kHoldoverEvents);
    out.since_edge_ms = wire::get_uint16(record.payload + pps_at::kSinceEdgeMs);
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
    out.address = wire::get_uint32(record.payload + burst_at::kAddress);
    out.tx_keyed_us = wire::get_uint16(record.payload + burst_at::kTxKeyedUs);
    out.tx_span_us = wire::get_uint16(record.payload + burst_at::kTxSpanUs);
    out.verdict = static_cast<radio::Verdict>(record.payload[burst_at::kVerdict]);
    out.source = static_cast<model::Source>(record.payload[burst_at::kSource]);
    out.band = static_cast<model::Band>(record.payload[burst_at::kBand]);
    out.channel = record.payload[burst_at::kChannel];
    out.bytes = record.payload[burst_at::kBytes];
    out.rssi_dbm = get_int8(record.payload + burst_at::kRssiDbm);
    out.key_offset_s = get_int8(record.payload + burst_at::kKeyOffsetS);
    out.tx_stage_margin_us = static_cast<int16_t>(
        get_int8(record.payload + burst_at::kTxStageMarginUs) * kStageMarginStepUs);
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
    out.pressure_mpa = wire::get_uint32(record.payload + barometer_at::kPressureMpa);
    out.altitude_mm = wire::get_int32(record.payload + barometer_at::kAltitudeMm);
    out.climb_mm_s = wire::get_int32(record.payload + barometer_at::kClimbMmS);
    out.temperature_dc = wire::get_int16(record.payload + barometer_at::kTemperatureDc);
    out.active = record.flagged(kBarometerFlagActive);
    out.temperature_valid = record.flagged(kBarometerFlagTemperatureValid);
    out.climb_adopted = record.flagged(kBarometerFlagClimbAdopted);
    return true;
}

inline bool read(const Record& record, Motion& out) {
    if (record.type != Type::Motion) return false;
    out = Motion{};
    out.slip_mg = wire::get_int16(record.payload + motion_at::kSlipMg);
    out.normal_mg = wire::get_int16(record.payload + motion_at::kNormalMg);
    out.lateral_mg = wire::get_int16(record.payload + motion_at::kLateralMg);
    out.longitudinal_mg = wire::get_int16(record.payload + motion_at::kLongitudinalMg);
    out.most_normal_mg = wire::get_int16(record.payload + motion_at::kMostNormalMg);
    out.least_normal_mg = wire::get_int16(record.payload + motion_at::kLeastNormalMg);
    out.imu_error = record.payload[motion_at::kImuError];
    out.sensor_error = record.payload[motion_at::kSensorError];
    out.slip_valid = record.flagged(kMotionFlagSlipValid);
    out.gload_valid = record.flagged(kMotionFlagGloadValid);
    out.fitted = record.flagged(kMotionFlagFitted);
    return true;
}

inline bool read(const Record& record, Power& out) {
    if (record.type != Type::Power) return false;
    out = Power{};
    out.cell_mv = wire::get_uint16(record.payload + power_at::kCellMv);
    out.supply_warnings = wire::get_uint16(record.payload + power_at::kSupplyWarnings);
    out.implausible = wire::get_uint16(record.payload + power_at::kImplausible);
    out.charge_warnings = wire::get_uint16(record.payload + power_at::kChargeWarnings);
    out.die_dc = wire::get_int16(record.payload + power_at::kDieDc);
    out.percent = record.payload[power_at::kPercent];
    out.power_level = static_cast<power::PowerLevel>(record.payload[power_at::kPowerLevel]);
    out.charge = static_cast<power::ChargeCondition>(record.payload[power_at::kCharge]);
    out.sample_offset_mv = get_int8(record.payload + power_at::kSampleOffsetMv);
    out.charging = record.flagged(kPowerFlagCharging);
    out.external_power = record.flagged(kPowerFlagExternalPower);
    out.battery_valid = record.flagged(kPowerFlagBatteryValid);
    out.die_valid = record.flagged(kPowerFlagDieValid);
    return true;
}

inline bool read(const Record& record, Contact& out) {
    if (record.type != Type::Contact) return false;
    out = Contact{};
    out.at_ms = wire::get_uint32(record.payload + contact_at::kAtMs);
    out.held_ms = wire::get_uint32(record.payload + contact_at::kHeldMs);
    out.contact = static_cast<events::Contact>(record.payload[contact_at::kContact]);
    out.gesture = record.payload[contact_at::kGesture];
    out.down = record.flagged(kContactFlagDown);
    return true;
}

inline bool read(const Record& record, Link& out) {
    if (record.type != Type::Link) return false;
    out = Link{};
    out.link_session = wire::get_uint16(record.payload + link_at::kLinkSession);
    out.payload_bytes = wire::get_uint16(record.payload + link_at::kPayloadBytes);
    out.frame_bytes = wire::get_uint16(record.payload + link_at::kFrameBytes);
    out.holder = wire::get_uint16(record.payload + link_at::kHolder);
    out.drops = wire::get_uint16(record.payload + link_at::kDrops);
    out.action = static_cast<LinkAction>(record.payload[link_at::kAction]);
    out.endpoint = static_cast<events::Endpoint>(record.payload[link_at::kEndpoint]);
    out.mtu_exchange_started_by =
        static_cast<events::MtuExchangeStarter>(record.payload[link_at::kMtuExchangeStartedBy]);
    out.claim_held = record.flagged(kLinkFlagClaimHeld);
    return true;
}

inline bool read(const Record& record, Switch& out) {
    if (record.type != Type::Switch) return false;
    out = Switch{};
    out.to_hz = wire::get_uint32(record.payload + switch_at::kToHz);
    out.margin_us = wire::get_int32(record.payload + switch_at::kMarginUs);
    out.took_us = wire::get_uint16(record.payload + switch_at::kTookUs);
    out.gap_us = wire::get_uint16(record.payload + switch_at::kGapUs);
    out.change = static_cast<SwitchKind>(record.payload[switch_at::kChange]);
    out.armed_ahead = record.flagged(kSwitchFlagArmedAhead);
    out.late = record.flagged(kSwitchFlagLate);
    return true;
}

inline bool read(const Record& record, Adsb& out) {
    if (record.type != Type::Adsb) return false;
    out = Adsb{};
    out.address = static_cast<uint32_t>(record.payload[adsb_at::kAddress]) |
                  (static_cast<uint32_t>(record.payload[adsb_at::kAddress + 1]) << 8) |
                  (static_cast<uint32_t>(record.payload[adsb_at::kAddress + 2]) << 16);
    out.adsb_version = (record.payload[adsb_at::kAdsbVersion] >> adsb_bits::kAdsbVersionShift) &
                       adsb_bits::kAdsbVersionMask;
    out.gva = (record.payload[adsb_at::kGva] >> adsb_bits::kGvaShift) & adsb_bits::kGvaMask;
    out.pressure_altitude_ft = wire::get_int32(record.payload + adsb_at::kPressureAltitudeFt);
    out.height_difference_ft = wire::get_int16(record.payload + adsb_at::kHeightDifferenceFt);
    out.height_difference_age_ms =
        wire::get_int16(record.payload + adsb_at::kHeightDifferenceAgeMs);
    out.gnss_relative_altitude_m =
        wire::get_int16(record.payload + adsb_at::kGnssRelativeAltitudeM);
    out.pressure_relative_altitude_m =
        wire::get_int16(record.payload + adsb_at::kPressureRelativeAltitudeM);
    out.adsb_version_valid = record.flagged(kAdsbFlagAdsbVersionValid);
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
    out.capabilities = wire::get_uint32(record.payload + boot_at::kCapabilities);
    out.firmware_build = wire::get_uint32(record.payload + boot_at::kFirmwareBuild);
    out.firmware_revision = wire::get_uint16(record.payload + boot_at::kFirmwareRevision);
    out.firmware_major = record.payload[boot_at::kFirmwareMajor];
    out.firmware_minor = record.payload[boot_at::kFirmwareMinor];
    out.reset = static_cast<power::ResetReason>(record.payload[boot_at::kReset]);
    out.image_state = static_cast<dfu::ImageState>(record.payload[boot_at::kImageState]);
    out.profile = static_cast<Profile>(record.payload[boot_at::kProfile]);
    out.profile_recorded = record.flagged(kBootFlagProfileRecorded);
    return true;
}

inline bool read(const Record& record, Config& out) {
    if (record.type != Type::Config) return false;
    out = Config{};
    out.address = wire::get_uint32(record.payload + config_at::kAddress);
    out.battery_offset_mv = wire::get_int16(record.payload + config_at::kBatteryOffsetMv);
    out.frequency_trim_e1_ppm = wire::get_int16(record.payload + config_at::kFrequencyTrimE1Ppm);
    out.aircraft_type = record.payload[config_at::kAircraftType];
    out.address_mapping_table = record.payload[config_at::kAddressMappingTable];
    out.settings_version = record.payload[config_at::kSettingsVersion];
    out.tx_power_dbm = get_int8(record.payload + config_at::kTxPowerDbm);
    out.pa_rated_dbm = get_int8(record.payload + config_at::kPaRatedDbm);
    out.alarm_setting = record.payload[config_at::kAlarmSetting];
    out.alarm_setting_recorded = record.flagged(kConfigFlagAlarmSettingRecorded);
    out.metric = record.flagged(kConfigFlagMetric);
    out.battery_trim_manual = record.flagged(kConfigFlagBatteryTrimManual);
    return true;
}

inline bool read(const Record& record, Dwell& out) {
    if (record.type != Type::Dwell) return false;
    out = Dwell{};
    out.frequency_hz = wire::get_uint32(record.payload + dwell_at::kFrequencyHz);
    out.start_ms = wire::get_uint16(record.payload + dwell_at::kStartMs);
    out.end_ms = wire::get_uint16(record.payload + dwell_at::kEndMs);
    out.phase_ms = wire::get_uint16(record.payload + dwell_at::kPhaseMs);
    out.duty_permille = wire::get_uint16(record.payload + dwell_at::kDutyPermille);
    out.state = static_cast<timing::SlotState>(record.payload[dwell_at::kState]);
    out.band = static_cast<model::Band>(record.payload[dwell_at::kBand]);
    out.refusal = static_cast<Refusal>(record.payload[dwell_at::kRefusal]);
    out.noise_dbm = get_int8(record.payload + dwell_at::kNoiseDbm);
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
    out.speed_mm_s = wire::get_int32(record.payload + flight_at::kSpeedMmS);
    out.climb_mm_s = wire::get_int32(record.payload + flight_at::kClimbMmS);
    out.altitude_msl_m = wire::get_int16(record.payload + flight_at::kAltitudeMslM);
    out.hdop_e2 = wire::get_uint16(record.payload + flight_at::kHdopE2);
    out.vdop_e2 = wire::get_uint16(record.payload + flight_at::kVdopE2);
    out.declared = static_cast<flight::FlightState>(record.payload[flight_at::kDeclared]);
    out.confirmed = static_cast<flight::FlightState>(record.payload[flight_at::kConfirmed]);
    out.fix_valid = record.flagged(kFlightFlagFixValid);
    out.rolling = record.flagged(kFlightFlagRolling);
    out.climb_valid = record.flagged(kFlightFlagClimbValid);
    out.tx_settled = record.flagged(kFlightFlagTxSettled);
    return true;
}

inline bool read(const Record& record, Traffic& out) {
    if (record.type != Type::Traffic) return false;
    out = Traffic{};
    out.address = wire::get_uint32(record.payload + traffic_at::kAddress);
    out.distance_m = wire::get_uint16(record.payload + traffic_at::kDistanceM);
    out.bearing_deg = wire::get_uint16(record.payload + traffic_at::kBearingDeg);
    out.relative_altitude_m = wire::get_int16(record.payload + traffic_at::kRelativeAltitudeM);
    out.closing_mps = wire::get_int16(record.payload + traffic_at::kClosingMps);
    out.alarm = static_cast<traffic::Level>(record.payload[traffic_at::kAlarm]);
    out.source = static_cast<model::Source>(record.payload[traffic_at::kSource]);
    out.rssi_dbm = get_int8(record.payload + traffic_at::kRssiDbm);
    out.tracked = record.payload[traffic_at::kTracked];
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
    out.waited_ms = wire::get_uint32(record.payload + write_at::kWaitedMs);
    out.phase_ms = wire::get_uint16(record.payload + write_at::kPhaseMs);
    out.requests = wire::get_uint16(record.payload + write_at::kRequests);
    out.writes = wire::get_uint16(record.payload + write_at::kWrites);
    out.forced = wire::get_uint16(record.payload + write_at::kForced);
    out.placement = static_cast<timing::DurableWriteVerdict>(record.payload[write_at::kPlacement]);
    out.kind = static_cast<power::DurableWrite>(record.payload[write_at::kKind]);
    out.pending = record.flagged(kWriteFlagPending);
    return true;
}

inline bool read(const Record& record, Screen& out) {
    if (record.type != Type::Screen) return false;
    out = Screen{};
    out.since_page_shown_ms = wire::get_uint32(record.payload + screen_at::kSincePageShownMs);
    out.page = record.payload[screen_at::kPage];
    out.mode = record.payload[screen_at::kMode];
    out.prompt = record.payload[screen_at::kPrompt];
    out.alarm = static_cast<traffic::Level>(record.payload[screen_at::kAlarm]);
    out.render_us = wire::get_uint32(record.payload + screen_at::kRenderUs);
    out.present_us = wire::get_uint32(record.payload + screen_at::kPresentUs);
    out.backlight = record.flagged(kScreenFlagBacklight);
    out.powered = record.flagged(kScreenFlagPowered);
    out.holding = record.flagged(kScreenFlagHolding);
    return true;
}

inline bool read(const Record& record, Loop& out) {
    if (record.type != Type::Loop) return false;
    out = Loop{};
    out.passes = wire::get_uint16(record.payload + loop_at::kPasses);
    out.busy_ms = wire::get_uint16(record.payload + loop_at::kBusyMs);
    out.worst_pass_us = wire::get_uint32(record.payload + loop_at::kWorstPassUs);
    out.worst_pass_phase_ms = wire::get_uint16(record.payload + loop_at::kWorstPassPhaseMs);
    out.worst_gap_ms = wire::get_uint16(record.payload + loop_at::kWorstGapMs);
    out.worst_gap_phase_ms = wire::get_uint16(record.payload + loop_at::kWorstGapPhaseMs);
    out.worst_service = static_cast<LoopService>(record.payload[loop_at::kWorstService]);
    out.worst_tick_ms = record.payload[loop_at::kWorstTickMs];
    return true;
}

inline bool read(const Record& record, Gap& out) {
    if (record.type != Type::Gap) return false;
    out = Gap{};
    out.dropped = wire::get_uint32(record.payload + gap_at::kDropped);
    out.span_ms = wire::get_uint32(record.payload + gap_at::kSpanMs);
    out.total = wire::get_uint32(record.payload + gap_at::kTotal);
    out.capacity = wire::get_uint16(record.payload + gap_at::kCapacity);
    return true;
}

inline bool read(const Record& record, End& out) {
    if (record.type != Type::End) return false;
    out = End{};
    out.records = wire::get_uint32(record.payload + end_at::kRecords);
    out.dropped = wire::get_uint32(record.payload + end_at::kDropped);
    return true;
}

inline bool read(const Record& record, Duty& out) {
    if (record.type != Type::Duty) return false;
    out = Duty{};
    out.panel_partial_refreshes =
        wire::get_uint16(record.payload + duty_at::kPanelPartialRefreshes);
    out.panel_full_refreshes = wire::get_uint16(record.payload + duty_at::kPanelFullRefreshes);
    out.backlight_ms = wire::get_uint16(record.payload + duty_at::kBacklightMs);
    out.rx_armed_ms = wire::get_uint16(record.payload + duty_at::kRxArmedMs);
    out.tx_keyed_ms = wire::get_uint16(record.payload + duty_at::kTxKeyedMs);
    out.ble_connected_ms = wire::get_uint16(record.payload + duty_at::kBleConnectedMs);
    out.annunciator_ms = wire::get_uint16(record.payload + duty_at::kAnnunciatorMs);
    return true;
}

inline bool read(const Record& record, Receiver& out) {
    if (record.type != Type::Receiver) return false;
    out = Receiver{};
    out.frames = wire::get_uint16(record.payload + receiver_at::kFrames);
    out.part_packets = wire::get_uint16(record.payload + receiver_at::kPartPackets);
    out.part_errors = wire::get_uint16(record.payload + receiver_at::kPartErrors);
    out.gain_step = record.payload[receiver_at::kGainStep];
    out.rearms = record.payload[receiver_at::kRearms];
    out.resets = record.payload[receiver_at::kResets];
    out.part_resets = record.payload[receiver_at::kPartResets];
    out.command_errors = record.payload[receiver_at::kCommandErrors];
    out.missed_edges = record.payload[receiver_at::kMissedEdges];
    out.noise_dbm = get_int8(record.payload + receiver_at::kNoiseDbm);
    out.threshold_dbm = get_int8(record.payload + receiver_at::kThresholdDbm);
    out.part_mode = static_cast<ports::ReceiverMode>(record.payload[receiver_at::kPartMode]);
    out.part_reset = static_cast<ports::ReceiverReset>(record.payload[receiver_at::kPartReset]);
    out.listening = record.flagged(kReceiverFlagListening);
    out.irq_armed = record.flagged(kReceiverFlagIrqArmed);
    out.probed = record.flagged(kReceiverFlagProbed);
    out.interrupt_pending = record.flagged(kReceiverFlagInterruptPending);
    out.front_end_kept = record.flagged(kReceiverFlagFrontEndKept);
    return true;
}

inline bool read(const Record& record, Reception& out) {
    if (record.type != Type::Reception) return false;
    out = Reception{};
    out.crc = wire::get_uint16(record.payload + reception_at::kCrc);
    out.unsupported = wire::get_uint16(record.payload + reception_at::kUnsupported);
    out.folded = wire::get_uint16(record.payload + reception_at::kFolded);
    out.positions = wire::get_uint16(record.payload + reception_at::kPositions);
    out.capture_refused = wire::get_uint16(record.payload + reception_at::kCaptureRefused);
    out.aircraft = record.payload[reception_at::kAircraft];
    out.tracked = record.payload[reception_at::kTracked];
    out.reference = static_cast<comms::ExportReference>(record.payload[reception_at::kReference]);
    out.framing_resyncs = record.payload[reception_at::kFramingResyncs];
    out.impossible_formats = wire::get_uint16(record.payload + reception_at::kImpossibleFormats);
    out.fix_valid = record.flagged(kReceptionFlagFixValid);
    return true;
}

inline bool read(const Record& record, Framing& out) {
    if (record.type != Type::Framing) return false;
    out = Framing{};
    out.overflow_drops = wire::get_uint16(record.payload + framing_at::kOverflowDrops);
    out.resync_drops = wire::get_uint16(record.payload + framing_at::kResyncDrops);
    out.queue_drops = wire::get_uint16(record.payload + framing_at::kQueueDrops);
    out.in_flight = wire::get_uint16(record.payload + framing_at::kInFlight);
    out.split_counts = wire::get_uint16(record.payload + framing_at::kSplitCounts);
    out.drain_busy_ms = wire::get_uint16(record.payload + framing_at::kDrainBusyMs);
    out.drains = wire::get_uint16(record.payload + framing_at::kDrains);
    out.stranded_resyncs = record.payload[framing_at::kStrandedResyncs];
    out.short_resyncs = record.payload[framing_at::kShortResyncs];
    return true;
}

inline bool read(const Record& record, Screening& out) {
    if (record.type != Type::Screening) return false;
    out = Screening{};
    out.squitters = wire::get_uint16(record.payload + screening_at::kSquitters);
    out.all_calls = wire::get_uint16(record.payload + screening_at::kAllCalls);
    out.replies = wire::get_uint16(record.payload + screening_at::kReplies);
    out.parity_refused = wire::get_uint16(record.payload + screening_at::kParityRefused);
    out.one_bit_errors = wire::get_uint16(record.payload + screening_at::kOneBitErrors);
    out.unmatched = wire::get_uint16(record.payload + screening_at::kUnmatched);
    out.worst_drain_us = wire::get_uint16(record.payload + screening_at::kWorstDrainUs);
    out.fifo_peak_frames = record.payload[screening_at::kFifoPeakFrames];
    out.queue_peak_frames = record.payload[screening_at::kQueuePeakFrames];
    return true;
}

}  // namespace skyblip::diagnostics

#endif
