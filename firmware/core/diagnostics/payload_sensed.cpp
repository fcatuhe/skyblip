#include "core/diagnostics/payload.h"
#include "core/flight/extrapolate.h"
#include "protocols/common/little_endian.h"

namespace skyblip::diagnostics {

namespace {

constexpr uint32_t kAdsbAddressMask = 0xFFFFFF;

static_assert(adsb_at::kGva == adsb_at::kAdsbVersion);

}  // namespace

Record record_of(const Gnss& value, const Instant& at) {
    Record record = framed(Type::Gnss, at);
    wire::put_uint16(record.payload + gnss_at::kSolutionPhaseMs, value.solution_phase_ms);
    wire::put_uint16(record.payload + gnss_at::kResidualM, value.residual_m);
    wire::put_uint16(record.payload + gnss_at::kHdopE2, value.hdop_e2);
    wire::put_uint16(record.payload + gnss_at::kVdopE2, value.vdop_e2);
    wire::put_uint16(record.payload + gnss_at::kStageS, clamp_uint16(value.stage_s));
    record.payload[gnss_at::kSatellites] = value.satellites;
    record.payload[gnss_at::kSatellitesInView] = value.satellites_in_view;
    record.payload[gnss_at::kFixMode] = value.fix_mode;
    record.payload[gnss_at::kReject] = static_cast<uint8_t>(value.reject);
    record.payload[gnss_at::kStage] = static_cast<uint8_t>(value.stage);
    set_flag(record.flags, kGnssFlagFixValid, value.fix_valid);
    set_flag(record.flags, kGnssFlagResidualValid, value.residual_valid);
    set_flag(record.flags, kGnssFlagPpsLocked, value.pps_locked);
    set_flag(record.flags, kGnssFlagGeoidMeasured, value.geoid_measured);
    set_flag(record.flags, kGnssFlagTxSettled, value.tx_settled);
    set_flag(record.flags, kGnssFlagLevelsFresh, value.levels_fresh);
    return record;
}

Record record_of(const Pps& value, const Instant& at) {
    Record record = framed(Type::Pps, at);
    wire::put_uint32(record.payload + pps_at::kIntervalUs, value.interval_us);
    wire::put_int32(record.payload + pps_at::kErrorUs, value.error_us);
    wire::put_uint32(record.payload + pps_at::kSamples, value.samples);
    wire::put_uint16(record.payload + pps_at::kHoldoverEvents, clamp_uint16(value.holdover_events));
    wire::put_uint16(record.payload + pps_at::kSinceEdgeMs, value.since_edge_ms);
    set_flag(record.flags, kPpsFlagLocked, value.locked);
    set_flag(record.flags, kPpsFlagUtcValid, value.utc_valid);
    return record;
}

Record record_of(const radio::Entry& value) {
    Instant at{};
    at.at_s = value.at_s;
    at.into_ms = value.into_ms;
    at.phase_valid = value.phase_valid;
    at.utc_dated = value.utc_dated;
    Record record = framed(Type::Burst, at);
    wire::put_uint32(record.payload + burst_at::kAddress, value.address);
    wire::put_uint16(record.payload + burst_at::kTxKeyedUs, value.tx_keyed_us);
    wire::put_uint16(record.payload + burst_at::kTxSpanUs, value.tx_span_us);
    record.payload[burst_at::kVerdict] = static_cast<uint8_t>(value.verdict);
    record.payload[burst_at::kSource] = static_cast<uint8_t>(value.source);
    record.payload[burst_at::kBand] = static_cast<uint8_t>(value.band);
    record.payload[burst_at::kChannel] = value.channel;
    record.payload[burst_at::kBytes] = value.bytes;
    put_int8(record.payload + burst_at::kRssiDbm, value.rssi_dbm);
    put_int8(record.payload + burst_at::kKeyOffsetS, value.key_offset_s);
    put_int8(record.payload + burst_at::kTxStageMarginUs,
             stage_margin_steps(value.tx_stage_margin_us));
    set_flag(record.flags, kBurstFlagAddressValid, value.address_valid);
    set_flag(record.flags, kBurstFlagRssiValid, value.rssi_valid);
    set_flag(record.flags, kBurstFlagAirborne, value.airborne);
    set_flag(record.flags, kBurstFlagTxSpanValid, value.tx_span_valid);
    set_flag(record.flags, kBurstFlagCallsign, value.callsign);
    set_flag(record.flags, kBurstFlagStatus, value.status);
    return record;
}

Record record_of(const Barometer& value, const Instant& at) {
    Record record = framed(Type::Barometer, at);
    wire::put_uint32(record.payload + barometer_at::kPressureMpa, value.pressure_mpa);
    wire::put_int32(record.payload + barometer_at::kAltitudeMm, value.altitude_mm);
    wire::put_int32(record.payload + barometer_at::kClimbMmS, value.climb_mm_s);
    wire::put_int16(record.payload + barometer_at::kTemperatureDc, value.temperature_dc);
    set_flag(record.flags, kBarometerFlagActive, value.active);
    set_flag(record.flags, kBarometerFlagTemperatureValid, value.temperature_valid);
    set_flag(record.flags, kBarometerFlagClimbAdopted, value.climb_adopted);
    return record;
}

Record record_of(const Motion& value, const Instant& at) {
    Record record = framed(Type::Motion, at);
    wire::put_int16(record.payload + motion_at::kSlipMg, value.slip_mg);
    wire::put_int16(record.payload + motion_at::kNormalMg, value.normal_mg);
    wire::put_int16(record.payload + motion_at::kLateralMg, value.lateral_mg);
    wire::put_int16(record.payload + motion_at::kLongitudinalMg, value.longitudinal_mg);
    wire::put_int16(record.payload + motion_at::kMostNormalMg, value.most_normal_mg);
    wire::put_int16(record.payload + motion_at::kLeastNormalMg, value.least_normal_mg);
    record.payload[motion_at::kImuError] = value.imu_error;
    record.payload[motion_at::kSensorError] = value.sensor_error;
    set_flag(record.flags, kMotionFlagSlipValid, value.slip_valid);
    set_flag(record.flags, kMotionFlagGloadValid, value.gload_valid);
    set_flag(record.flags, kMotionFlagFitted, value.fitted);
    return record;
}

Record record_of(const Power& value, const Instant& at) {
    Record record = framed(Type::Power, at);
    wire::put_uint16(record.payload + power_at::kCellMv, value.cell_mv);
    wire::put_uint16(record.payload + power_at::kSupplyWarnings,
                     clamp_uint16(value.supply_warnings));
    wire::put_uint16(record.payload + power_at::kImplausible, clamp_uint16(value.implausible));
    wire::put_uint16(record.payload + power_at::kChargeWarnings,
                     clamp_uint16(value.charge_warnings));
    wire::put_int16(record.payload + power_at::kDieDc, value.die_dc);
    record.payload[power_at::kPercent] = value.percent;
    record.payload[power_at::kPowerLevel] = static_cast<uint8_t>(value.power_level);
    record.payload[power_at::kCharge] = static_cast<uint8_t>(value.charge);
    record.payload[power_at::kSampleOffsetMv] = static_cast<uint8_t>(value.sample_offset_mv);
    set_flag(record.flags, kPowerFlagCharging, value.charging);
    set_flag(record.flags, kPowerFlagExternalPower, value.external_power);
    set_flag(record.flags, kPowerFlagBatteryValid, value.battery_valid);
    set_flag(record.flags, kPowerFlagDieValid, value.die_valid);
    return record;
}

Record record_of(const Contact& value, const Instant& at) {
    Record record = framed(Type::Contact, at);
    wire::put_uint32(record.payload + contact_at::kAtMs, value.at_ms);
    wire::put_uint32(record.payload + contact_at::kHeldMs, value.held_ms);
    record.payload[contact_at::kContact] = static_cast<uint8_t>(value.contact);
    record.payload[contact_at::kGesture] = value.gesture;
    set_flag(record.flags, kContactFlagDown, value.down);
    return record;
}

Record record_of(const Link& value, const Instant& at) {
    Record record = framed(Type::Link, at);
    wire::put_uint16(record.payload + link_at::kLinkSession, value.link_session);
    wire::put_uint16(record.payload + link_at::kPayloadBytes, value.payload_bytes);
    wire::put_uint16(record.payload + link_at::kFrameBytes, value.frame_bytes);
    wire::put_uint16(record.payload + link_at::kHolder, value.holder);
    wire::put_uint16(record.payload + link_at::kDrops, clamp_uint16(value.drops));
    record.payload[link_at::kAction] = static_cast<uint8_t>(value.action);
    record.payload[link_at::kEndpoint] = static_cast<uint8_t>(value.endpoint);
    record.payload[link_at::kMtuExchangeStartedBy] =
        static_cast<uint8_t>(value.mtu_exchange_started_by);
    set_flag(record.flags, kLinkFlagClaimHeld, value.claim_held);
    return record;
}

Record record_of(const Switch& value, const Instant& at) {
    Record record = framed(Type::Switch, at);
    wire::put_uint32(record.payload + switch_at::kToHz, value.to_hz);
    wire::put_int32(record.payload + switch_at::kMarginUs, value.margin_us);
    wire::put_uint16(record.payload + switch_at::kTookUs, value.took_us);
    wire::put_uint16(record.payload + switch_at::kGapUs, value.gap_us);
    record.payload[switch_at::kChange] = static_cast<uint8_t>(value.change);
    set_flag(record.flags, kSwitchFlagArmedAhead, value.armed_ahead);
    set_flag(record.flags, kSwitchFlagLate, value.late);
    return record;
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
    record.payload[adsb_at::kAddress] = static_cast<uint8_t>(address);
    record.payload[adsb_at::kAddress + 1] = static_cast<uint8_t>(address >> 8);
    record.payload[adsb_at::kAddress + 2] = static_cast<uint8_t>(address >> 16);
    record.payload[adsb_at::kAdsbVersion] = static_cast<uint8_t>(
        ((value.adsb_version & adsb_bits::kAdsbVersionMask) << adsb_bits::kAdsbVersionShift) |
        ((value.gva & adsb_bits::kGvaMask) << adsb_bits::kGvaShift));
    wire::put_int32(record.payload + adsb_at::kPressureAltitudeFt, value.pressure_altitude_ft);
    wire::put_int16(record.payload + adsb_at::kHeightDifferenceFt,
                    clamp_int16(value.height_difference_ft));
    wire::put_int16(record.payload + adsb_at::kHeightDifferenceAgeMs,
                    clamp_int16(value.height_difference_age_ms));
    wire::put_int16(record.payload + adsb_at::kGnssRelativeAltitudeM,
                    clamp_int16(value.gnss_relative_altitude_m));
    wire::put_int16(record.payload + adsb_at::kPressureRelativeAltitudeM,
                    clamp_int16(value.pressure_relative_altitude_m));
    set_flag(record.flags, kAdsbFlagAdsbVersionValid, value.adsb_version_valid);
    set_flag(record.flags, kAdsbFlagGvaValid, value.gva_valid);
    set_flag(record.flags, kAdsbFlagPressureAltitudeValid, value.pressure_altitude_valid);
    set_flag(record.flags, kAdsbFlagHeightDifferenceValid, value.height_difference_valid);
    set_flag(record.flags, kAdsbFlagGnssRelativeAltitudeValid, value.gnss_relative_altitude_valid);
    set_flag(record.flags, kAdsbFlagPressureRelativeAltitudeValid,
             value.pressure_relative_altitude_valid);
    return record;
}

Receiver receiver_of(const ports::AdsbReceiverStatistics& statistics, bool listening) {
    Receiver value{};
    value.frames = statistics.frames;
    value.rearms = statistics.rearms;
    value.resets = statistics.resets;
    value.part_resets = statistics.part_resets;
    value.command_errors = statistics.command_errors;
    value.missed_edges = statistics.missed_edges;
    value.listening = listening;
    value.irq_armed = statistics.irq_armed;
    const ports::ReceiverProbe& probe = statistics.probe;
    value.probed = probe.valid;
    if (!probe.valid) return value;
    value.part_packets = probe.packets;
    value.part_errors = probe.errors;
    value.noise_dbm = probe.noise_dbm;
    value.threshold_dbm = probe.threshold_dbm;
    value.part_mode = probe.mode;
    value.part_reset = probe.reset;
    value.gain_step = probe.gain_step;
    value.interrupt_pending = probe.interrupt_pending;
    value.front_end_kept = probe.front_end_kept;
    value.boosted = probe.boosted;
    return value;
}

Record record_of(const Receiver& value, const Instant& at) {
    Record record = framed(Type::Receiver, at);
    wire::put_uint16(record.payload + receiver_at::kFrames, static_cast<uint16_t>(value.frames));
    wire::put_uint16(record.payload + receiver_at::kPartPackets,
                     static_cast<uint16_t>(value.part_packets));
    wire::put_uint16(record.payload + receiver_at::kPartErrors, value.part_errors);
    record.payload[receiver_at::kGainStep] = value.gain_step;
    record.payload[receiver_at::kRearms] = static_cast<uint8_t>(value.rearms);
    record.payload[receiver_at::kResets] = static_cast<uint8_t>(value.resets);
    record.payload[receiver_at::kPartResets] = static_cast<uint8_t>(value.part_resets);
    record.payload[receiver_at::kCommandErrors] = static_cast<uint8_t>(value.command_errors);
    record.payload[receiver_at::kMissedEdges] = static_cast<uint8_t>(value.missed_edges);
    put_int8(record.payload + receiver_at::kNoiseDbm, value.noise_dbm);
    put_int8(record.payload + receiver_at::kThresholdDbm, value.threshold_dbm);
    record.payload[receiver_at::kPartMode] = static_cast<uint8_t>(value.part_mode);
    record.payload[receiver_at::kPartReset] = static_cast<uint8_t>(value.part_reset);
    set_flag(record.flags, kReceiverFlagListening, value.listening);
    set_flag(record.flags, kReceiverFlagIrqArmed, value.irq_armed);
    set_flag(record.flags, kReceiverFlagProbed, value.probed);
    set_flag(record.flags, kReceiverFlagInterruptPending, value.interrupt_pending);
    set_flag(record.flags, kReceiverFlagFrontEndKept, value.front_end_kept);
    set_flag(record.flags, kReceiverFlagBoosted, value.boosted);
    return record;
}

}  // namespace skyblip::diagnostics
