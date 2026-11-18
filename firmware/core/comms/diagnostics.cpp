#include "core/comms/diagnostics.h"

#include <algorithm>
#include <iterator>

#include "core/util/cbor.h"

namespace skyblip::comms {

int whole_celsius(int16_t temperature_dc) {
    const int16_t bias = temperature_dc >= 0 ? 5 : -5;
    return (temperature_dc + bias) / 10;
}

DiagnosticsReport::DiagnosticsReport(const Diagnostics& diagnostics, link::Command command)
    : command_(command) {
    build(diagnostics, nullptr);
}

DiagnosticsReport::DiagnosticsReport(const Diagnostics& diagnostics, link::Command command,
                                     Group only)
    : command_(command) {
    build(diagnostics, &only);
}

// THE TABLE. The link carries this list in this order. Every value is a plain
// read of something the device already keeps: nothing here computes, samples or
// clears anything.
void DiagnosticsReport::build(const Diagnostics& diagnostics, const Group* only) {
    const bool system = only == nullptr || *only == Group::System;
    const bool radio = only == nullptr || *only == Group::Radio;
    const bool traffic = only == nullptr || *only == Group::Traffic;
    const bool gnss = only == nullptr || *only == Group::Gnss;
    const bool power = only == nullptr || *only == Group::Power;
    const bool adsb = only == nullptr || *only == Group::Adsb;
    const bool capture = only == nullptr || *only == Group::Capture;

    if (system) {
        add(Group::System, link::diagnostics_key::kUptimeS, Kind::Unsigned, diagnostics.uptime_s);
        add(Group::System, link::diagnostics_key::kReset, Kind::Unsigned,
            static_cast<int64_t>(diagnostics.reset));
        add(Group::System, link::diagnostics_key::kLinkDrops, Kind::Unsigned,
            diagnostics.link_drops);
    }

    if (radio) {
        add(Group::Radio, link::diagnostics_key::kNoiseDbm, Kind::Integer, diagnostics.noise_dbm);
        add(Group::Radio, link::diagnostics_key::kDutyPermille, Kind::Unsigned,
            diagnostics.duty_permille);
        add(Group::Radio, link::diagnostics_key::kRxOk, Kind::Unsigned, diagnostics.rx_ok);
        add(Group::Radio, link::diagnostics_key::kRxBad, Kind::Unsigned, diagnostics.rx_bad);
        add(Group::Radio, link::diagnostics_key::kRxWait, Kind::Unsigned, diagnostics.rx_wait);
        add(Group::Radio, link::diagnostics_key::kRxType, Kind::Unsigned, diagnostics.rx_type);
        add(Group::Radio, link::diagnostics_key::kRxUnframed, Kind::Unsigned,
            diagnostics.rx_unframed);
        add(Group::Radio, link::diagnostics_key::kRxMiskeyed, Kind::Unsigned,
            diagnostics.rx_miskeyed);
        add(Group::Radio, link::diagnostics_key::kRxNoise, Kind::Unsigned, diagnostics.rx_noise);
        add(Group::Radio, link::diagnostics_key::kRxNamed, Kind::Unsigned, diagnostics.rx_named);
        add(Group::Radio, link::diagnostics_key::kTxOk, Kind::Unsigned, diagnostics.tx_ok);
        add(Group::Radio, link::diagnostics_key::kTxLost, Kind::Unsigned, diagnostics.tx_lost);
        add(Group::Radio, link::diagnostics_key::kTxNamed, Kind::Unsigned, diagnostics.tx_named);
        add(Group::Radio, link::diagnostics_key::kTxKeyedUs, Kind::Unsigned,
            diagnostics.tx_keyed_us);
        add(Group::Radio, link::diagnostics_key::kTxSpanUs, Kind::Unsigned, diagnostics.tx_span_us);
        // The range gate's refusals read out with the radio and not with the
        // table, because what they measure is this receiver's link budget: a rate
        // that climbs is a decoder correcting a frame into a position no radio
        // this size could have heard (core/traffic/sanity.h).
        add(Group::Radio, link::diagnostics_key::kRangeRefused, Kind::Unsigned,
            diagnostics.range_refused);
        add(Group::Radio, link::diagnostics_key::kDio1Armed, Kind::Boolean,
            diagnostics.dio1_armed ? 1 : 0);
        add(Group::Radio, link::diagnostics_key::kDio1Edges, Kind::Unsigned,
            diagnostics.dio1_edges);
        add(Group::Radio, link::diagnostics_key::kDio1Missed, Kind::Unsigned,
            diagnostics.dio1_missed);
        // INFO: fc 13nov26 a frame's keys climb, so the counters numbered last are added last
        add(Group::Radio, link::diagnostics_key::kTxHeldForB5, Kind::Unsigned,
            diagnostics.tx_held_for_b5);
        add(Group::Radio, link::diagnostics_key::kRxStatus, Kind::Unsigned, diagnostics.rx_status);
        add(Group::Radio, link::diagnostics_key::kUplinkStale, Kind::Unsigned,
            diagnostics.uplink_stale);
        add(Group::Radio, link::diagnostics_key::kTxGivenUp, Kind::Unsigned,
            diagnostics.tx_given_up);
        add(Group::Radio, link::diagnostics_key::kTxDeferred, Kind::Unsigned,
            diagnostics.tx_deferred);
        add(Group::Radio, link::diagnostics_key::kTxForced, Kind::Unsigned, diagnostics.tx_forced);
        add(Group::Radio, link::diagnostics_key::kTxBusyMissed, Kind::Unsigned,
            diagnostics.tx_busy_missed);
    }

    if (traffic) {
        add(Group::Traffic, link::diagnostics_key::kTracked, Kind::Unsigned, diagnostics.tracked);
        add(Group::Traffic, link::diagnostics_key::kAlarm, Kind::Unsigned, diagnostics.alarm);
        add(Group::Traffic, link::diagnostics_key::kUplinkFrames, Kind::Unsigned,
            diagnostics.uplink_frames);
        add(Group::Traffic, link::diagnostics_key::kUplinkBad, Kind::Unsigned,
            diagnostics.uplink_bad);
        add(Group::Traffic, link::diagnostics_key::kUplinkTargets, Kind::Unsigned,
            diagnostics.uplink_targets);
    }

    if (gnss) {
        add(Group::Gnss, link::diagnostics_key::kFixes, Kind::Unsigned, diagnostics.gnss_fixes);
        add(Group::Gnss, link::diagnostics_key::kValid, Kind::Boolean,
            diagnostics.fix_valid ? 1 : 0);
        // A receiver silently at another rate is a GNSS-less device that looks
        // fitted, and it is the top support case this part will generate.
        add(Group::Gnss, link::diagnostics_key::kBaud, Kind::Unsigned, diagnostics.gnss_baud);
        add(Group::Gnss, link::diagnostics_key::kConfig, Kind::Unsigned,
            static_cast<int64_t>(diagnostics.gnss_config));
        add(Group::Gnss, link::diagnostics_key::kRaises, Kind::Unsigned, diagnostics.gnss_raises);
        add(Group::Gnss, link::diagnostics_key::kRaiseFallbacks, Kind::Unsigned,
            diagnostics.gnss_raise_fallbacks);
        // INFO: fc 19sep26 the phase a solution lands at, which kFixMaxAgeMs refuses a burst over
        if (diagnostics.gnss_solution_phase_valid)
            add(Group::Gnss, link::diagnostics_key::kSolutionPhaseMs, Kind::Unsigned,
                diagnostics.gnss_solution_phase_ms);
        add(Group::Gnss, link::diagnostics_key::kOverruns, Kind::Unsigned,
            diagnostics.gnss_overruns);
        add(Group::Gnss, link::diagnostics_key::kIdentified, Kind::Boolean,
            diagnostics.gnss_identified ? 1 : 0);
        add(Group::Gnss, link::diagnostics_key::kFirmware, Kind::Text, 0,
            diagnostics.gnss_firmware);
        add(Group::Gnss, link::diagnostics_key::kReject, Kind::Unsigned,
            static_cast<int64_t>(diagnostics.gnss_reject));
        add(Group::Gnss, link::diagnostics_key::kRejected, Kind::Unsigned,
            diagnostics.gnss_rejected);
        // Absent rather than zero: a device with no two consecutive fixes has no
        // residual, and 0 m is what a perfect one reports.
        if (diagnostics.residual_valid)
            add(Group::Gnss, link::diagnostics_key::kResidualM, Kind::Unsigned,
                diagnostics.residual_m);
    }

    if (power) {
        add(Group::Power, link::diagnostics_key::kCellMv, Kind::Unsigned,
            diagnostics.battery.cell_mv);
        add(Group::Power, link::diagnostics_key::kPercent, Kind::Unsigned,
            diagnostics.battery.percent);
        add(Group::Power, link::diagnostics_key::kBatteryValid, Kind::Boolean,
            diagnostics.battery.valid ? 1 : 0);
        add(Group::Power, link::diagnostics_key::kCharging, Kind::Boolean,
            diagnostics.battery.charging ? 1 : 0);
        add(Group::Power, link::diagnostics_key::kPowerLevel, Kind::Unsigned,
            static_cast<int64_t>(diagnostics.power_level));
        add(Group::Power, link::diagnostics_key::kSupplyWarnings, Kind::Unsigned,
            diagnostics.supply_warnings);
        add(Group::Power, link::diagnostics_key::kImplausible, Kind::Unsigned,
            diagnostics.battery_implausible);
        // Same rule as the status reply, and the same key: no reading is no key,
        // never a zero, because 0.0 C is a plausible hangar morning.
        if (diagnostics.die_valid)
            add(Group::Power, link::diagnostics_key::kDieTemperatureC, Kind::Integer,
                whole_celsius(diagnostics.die_dc));
        if (diagnostics.charge != power::ChargeCondition::Unknown)
            add(Group::Power, link::diagnostics_key::kCharge, Kind::Unsigned,
                static_cast<int64_t>(diagnostics.charge));
        add(Group::Power, link::diagnostics_key::kChargeWarnings, Kind::Unsigned,
            diagnostics.charge_warnings);
    }

    if (adsb && diagnostics.adsb_fitted) {
        const ports::AdsbReceiverStatistics& receiver = diagnostics.adsb;
        add(Group::Adsb, link::diagnostics_key::kFrames, Kind::Unsigned, receiver.frames);
        add(Group::Adsb, link::diagnostics_key::kFrameRateHz, Kind::Unsigned,
            receiver.frame_rate_hz);
        add(Group::Adsb, link::diagnostics_key::kDropped, Kind::Unsigned, receiver.dropped);
        add(Group::Adsb, link::diagnostics_key::kRearms, Kind::Unsigned, receiver.rearms);
        add(Group::Adsb, link::diagnostics_key::kResets, Kind::Unsigned, receiver.resets);
        add(Group::Adsb, link::diagnostics_key::kCommandErrors, Kind::Unsigned,
            receiver.command_errors);
        add(Group::Adsb, link::diagnostics_key::kErrors, Kind::Unsigned, receiver.device_errors);
        std::copy(std::begin(receiver.rssi), std::end(receiver.rssi), std::begin(adsb_rssi_));
        add_array(Group::Adsb, link::diagnostics_key::kRssiDbm, Array::AdsbRssi);
        // INFO: fc 12nov26 after rssi_dbm, because a frame's keys ascend and these came later
        add(Group::Adsb, link::diagnostics_key::kCrc, Kind::Unsigned, diagnostics.adsb_crc);
        add(Group::Adsb, link::diagnostics_key::kUnsupported, Kind::Unsigned,
            diagnostics.adsb_unsupported);
        add(Group::Adsb, link::diagnostics_key::kFolded, Kind::Unsigned, diagnostics.adsb_folded);
        add(Group::Adsb, link::diagnostics_key::kAircraft, Kind::Unsigned,
            diagnostics.adsb_aircraft);
        std::copy(std::begin(diagnostics.adsb_formats), std::end(diagnostics.adsb_formats),
                  std::begin(adsb_formats_));
        add_array(Group::Adsb, link::diagnostics_key::kFormats, Array::AdsbFormats);
        add(Group::Adsb, link::diagnostics_key::kTransponders, Kind::Unsigned,
            diagnostics.adsb_transponders);
        add(Group::Adsb, link::diagnostics_key::kModeS, Kind::Unsigned, diagnostics.adsb_mode_s);
        add(Group::Adsb, link::diagnostics_key::kModeSAltitude, Kind::Unsigned,
            diagnostics.adsb_mode_s_altitude);
        add(Group::Adsb, link::diagnostics_key::kUnmatched, Kind::Unsigned,
            diagnostics.adsb_unmatched);
    }

    if (capture && diagnostics.capture_available) add_capture(diagnostics);
}

void DiagnosticsReport::add_capture(const Diagnostics& diagnostics) {
    namespace key = link::diagnostics_key;
    add(Group::Capture, key::kArmed, Kind::Boolean, diagnostics.capture_armed);
    if (diagnostics.capture_armed)
        add(Group::Capture, key::kProfile, Kind::Unsigned,
            static_cast<int64_t>(diagnostics.capture_profile));
    add(Group::Capture, key::kSession, Kind::Unsigned, diagnostics.capture_session);
    add(Group::Capture, key::kRecords, Kind::Unsigned, diagnostics.capture_records);
    add(Group::Capture, key::kRecordsDropped, Kind::Unsigned, diagnostics.capture_dropped);
    add(Group::Capture, key::kSectors, Kind::Unsigned, diagnostics.capture_sectors);
    add(Group::Capture, key::kKeepsS, Kind::Unsigned, diagnostics.capture_keeps_s);
    add(Group::Capture, key::kTakesSectors, Kind::Unsigned, diagnostics.capture_takes_sectors);
    add(Group::Capture, key::kEvictsFlights, Kind::Unsigned, diagnostics.capture_evicts_flights);
    add(Group::Capture, key::kKeepsFullS, Kind::Unsigned, diagnostics.capture_keeps_full_s);
    add(Group::Capture, key::kKeepsPowerRunS, Kind::Unsigned,
        diagnostics.capture_keeps_power_run_s);
}

const char* capture_profile_word(CaptureProfile profile) {
    switch (profile) {
        case CaptureProfile::Full: return "FULL";
        case CaptureProfile::PowerRun: return "POWER RUN";
        case CaptureProfile::FlightRun: return "FLIGHT RUN";
        case CaptureProfile::Stop: break;
    }
    return "STOP";
}

void DiagnosticsReport::add(Group group, uint32_t key, Kind kind, int64_t value, const char* text) {
    if (count_ >= kMaxFields) return;
    fields_[count_++] = Field{key, text, value, kind, group};
}

// INFO: fc 18nov26 a field names its array, not its address, so a copied report reads its own
void DiagnosticsReport::add_array(Group group, uint32_t key, Array array) {
    add(group, key, Kind::UnsignedArray, static_cast<int64_t>(array));
}

const uint32_t* DiagnosticsReport::values_of(const Field& field, int& count) const {
    if (static_cast<Array>(field.value) == Array::AdsbFormats) {
        count = adsb::kDownlinkFormats;
        return adsb_formats_;
    }
    count = kAdsbRssiCount;
    return adsb_rssi_;
}

int DiagnosticsReport::field_bytes(const Field& field) const {
    switch (field.kind) {
        case Kind::Unsigned:
            return cbor::unsigned_entry_bytes(field.key, static_cast<uint64_t>(field.value));
        case Kind::Integer: return cbor::integer_entry_bytes(field.key, field.value);
        case Kind::Boolean: return cbor::boolean_entry_bytes(field.key);
        case Kind::Text: return cbor::text_entry_bytes(field.key, field.text);
        case Kind::UnsignedArray: {
            int count = 0;
            const uint32_t* values = values_of(field, count);
            return cbor::unsigned_array_entry_bytes(field.key, values, count);
        }
    }
    return 0;
}

// Asked before the first frame goes out, so the answer is all or nothing: a host
// holding four frames of a five-frame dump, with no fifth one coming, has been
// told a fault it cannot see is absent.
bool DiagnosticsReport::fits(int payload_bytes) const {
    int widest_field = 0;
    for (int i = 0; i < count_; i++) widest_field = std::max(field_bytes(fields_[i]), widest_field);
    cbor::MapBudget budget(payload_bytes);
    return budget.take(
               cbor::unsigned_entry_bytes(link::kCommandKey, static_cast<uint8_t>(command_))) &&
           budget.take(
               cbor::unsigned_entry_bytes(link::diagnostics_key::kGroup, kGroupCount - 1)) &&
           budget.take(cbor::unsigned_entry_bytes(link::diagnostics_key::kPart, count_)) &&
           budget.take(cbor::boolean_entry_bytes(link::diagnostics_key::kMore)) &&
           budget.take(widest_field);
}

int DiagnosticsReport::next_frame(int payload_bytes, uint8_t* out, int capacity) {
    if (exhausted()) return 0;

    const int room = std::min(payload_bytes, capacity);
    const Group group = fields_[at_].group;
    cbor::MapBudget budget(room);
    budget.take(cbor::unsigned_entry_bytes(link::kCommandKey, static_cast<uint8_t>(command_)));
    budget.take(
        cbor::unsigned_entry_bytes(link::diagnostics_key::kGroup, static_cast<uint8_t>(group)));
    budget.take(cbor::unsigned_entry_bytes(link::diagnostics_key::kPart, part_));
    budget.take(cbor::boolean_entry_bytes(link::diagnostics_key::kMore));
    int packed = 0;
    while (at_ + packed < count_ && fields_[at_ + packed].group == group &&
           budget.take(field_bytes(fields_[at_ + packed]))) {
        packed++;
    }
    if (packed == 0) return 0;

    cbor::Writer writer(out, room);
    writer.add_code(link::kCommandKey, command_);
    writer.add_code(link::diagnostics_key::kGroup, group);
    writer.add_unsigned(link::diagnostics_key::kPart, part_);
    writer.add_boolean(link::diagnostics_key::kMore, at_ + packed < count_);
    for (int i = 0; i < packed; i++) {
        const Field& field = fields_[at_ + i];
        switch (field.kind) {
            case Kind::Unsigned:
                writer.add_unsigned(field.key, static_cast<uint64_t>(field.value));
                break;
            case Kind::Integer: writer.add_integer(field.key, field.value); break;
            case Kind::Boolean: writer.add_boolean(field.key, field.value != 0); break;
            case Kind::Text: writer.add_text(field.key, field.text); break;
            case Kind::UnsignedArray: {
                int count = 0;
                const uint32_t* values = values_of(field, count);
                writer.add_unsigned_array(field.key, values, count);
                break;
            }
        }
    }
    const int bytes = writer.finish();
    if (bytes == 0) return 0;

    at_ += packed;
    part_++;
    return bytes;
}

}  // namespace skyblip::comms
