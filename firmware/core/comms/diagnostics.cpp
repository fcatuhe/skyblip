#include "core/comms/diagnostics.h"

#include <algorithm>

#include "core/util/cbor.h"

namespace skyblip::comms {

namespace {

int write_text(char* out, const char* text) {
    int written = 0;
    while (text[written] != 0) {
        out[written] = text[written];
        written++;
    }
    return written;
}

int number_bytes(int64_t value) {
    int bytes = value < 0 ? 1 : 0;
    uint64_t magnitude =
        value < 0 ? 0 - static_cast<uint64_t>(value) : static_cast<uint64_t>(value);
    do {
        bytes++;
        magnitude /= 10;
    } while (magnitude != 0);
    return bytes;
}

int write_number(char* out, int64_t value) {
    const int written = number_bytes(value);
    uint64_t magnitude =
        value < 0 ? 0 - static_cast<uint64_t>(value) : static_cast<uint64_t>(value);
    int at = written;
    do {
        out[--at] = static_cast<char>('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude != 0);
    if (value < 0) out[0] = '-';
    return written;
}

const char* boolean_text(bool value) { return value ? "true" : "false"; }

}  // namespace

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

// THE TABLE. Both surfaces are this list, in this order, so a field added here
// reaches the console and the link in one edit and cannot reach one of them under
// a different name. Every value is a plain read of something the device already
// keeps: nothing here computes, samples or clears anything.
void DiagnosticsReport::build(const Diagnostics& diagnostics, const Group* only) {
    const bool system = only == nullptr || *only == Group::System;
    const bool radio = only == nullptr || *only == Group::Radio;
    const bool traffic = only == nullptr || *only == Group::Traffic;
    const bool gnss = only == nullptr || *only == Group::Gnss;
    const bool power = only == nullptr || *only == Group::Power;

    if (system) {
        add(Group::System, "uptime_s", link::diagnostics_key::kUptimeS, Kind::Unsigned,
            diagnostics.uptime_s);
        add(Group::System, "reset", link::diagnostics_key::kReset, Kind::Code,
            static_cast<int64_t>(diagnostics.reset), power::to_string(diagnostics.reset));
        add(Group::System, "link_drops", link::diagnostics_key::kLinkDrops, Kind::Unsigned,
            diagnostics.link_drops);
        add(Group::System, "mtu_exchanged_by_central",
            link::diagnostics_key::kMtuExchangedByCentral, Kind::Unsigned,
            diagnostics.mtu_exchanged_by_central);
        add(Group::System, "mtu_exchanged_by_unit", link::diagnostics_key::kMtuExchangedByUnit,
            Kind::Unsigned, diagnostics.mtu_exchanged_by_unit);
        add(Group::System, "mtu_never_exchanged", link::diagnostics_key::kMtuNeverExchanged,
            Kind::Unsigned, diagnostics.mtu_never_exchanged);
        add(Group::System, "mtu_requests_failed", link::diagnostics_key::kMtuRequestsFailed,
            Kind::Unsigned, diagnostics.mtu_requests_failed);
        add(Group::System, "replies_too_wide", link::diagnostics_key::kRepliesTooWide,
            Kind::Unsigned, diagnostics.replies_too_wide);
    }

    if (radio) {
        add(Group::Radio, "noise_dbm", link::diagnostics_key::kNoiseDbm, Kind::Integer,
            diagnostics.noise_dbm);
        add(Group::Radio, "duty_permille", link::diagnostics_key::kDutyPermille, Kind::Unsigned,
            diagnostics.duty_permille);
        add(Group::Radio, "rx_ok", link::diagnostics_key::kRxOk, Kind::Unsigned, diagnostics.rx_ok);
        add(Group::Radio, "rx_bad", link::diagnostics_key::kRxBad, Kind::Unsigned,
            diagnostics.rx_bad);
        add(Group::Radio, "rx_wait", link::diagnostics_key::kRxWait, Kind::Unsigned,
            diagnostics.rx_wait);
        add(Group::Radio, "rx_type", link::diagnostics_key::kRxType, Kind::Unsigned,
            diagnostics.rx_type);
        add(Group::Radio, "rx_unframed", link::diagnostics_key::kRxUnframed, Kind::Unsigned,
            diagnostics.rx_unframed);
        add(Group::Radio, "rx_miskeyed", link::diagnostics_key::kRxMiskeyed, Kind::Unsigned,
            diagnostics.rx_miskeyed);
        add(Group::Radio, "rx_noise", link::diagnostics_key::kRxNoise, Kind::Unsigned,
            diagnostics.rx_noise);
        add(Group::Radio, "rx_named", link::diagnostics_key::kRxNamed, Kind::Unsigned,
            diagnostics.rx_named);
        add(Group::Radio, "tx_ok", link::diagnostics_key::kTxOk, Kind::Unsigned, diagnostics.tx_ok);
        add(Group::Radio, "tx_lost", link::diagnostics_key::kTxLost, Kind::Unsigned,
            diagnostics.tx_lost);
        add(Group::Radio, "tx_named", link::diagnostics_key::kTxNamed, Kind::Unsigned,
            diagnostics.tx_named);
        add(Group::Radio, "tx_keyed_us", link::diagnostics_key::kTxKeyedUs, Kind::Unsigned,
            diagnostics.tx_keyed_us);
        add(Group::Radio, "tx_span_us", link::diagnostics_key::kTxSpanUs, Kind::Unsigned,
            diagnostics.tx_span_us);
        // The range gate's refusals read out with the radio and not with the
        // table, because what they measure is this receiver's link budget: a rate
        // that climbs is a decoder correcting a frame into a position no radio
        // this size could have heard (core/traffic/sanity.h).
        add(Group::Radio, "range_refused", link::diagnostics_key::kRangeRefused, Kind::Unsigned,
            diagnostics.range_refused);
        add(Group::Radio, "dio1_armed", link::diagnostics_key::kDio1Armed, Kind::Boolean,
            diagnostics.dio1_armed ? 1 : 0);
        add(Group::Radio, "dio1_edges", link::diagnostics_key::kDio1Edges, Kind::Unsigned,
            diagnostics.dio1_edges);
        add(Group::Radio, "dio1_missed", link::diagnostics_key::kDio1Missed, Kind::Unsigned,
            diagnostics.dio1_missed);
    }

    if (traffic) {
        add(Group::Traffic, "tracked", link::diagnostics_key::kTracked, Kind::Unsigned,
            diagnostics.tracked);
        add(Group::Traffic, "alarm", link::diagnostics_key::kAlarm, Kind::Unsigned,
            diagnostics.alarm);
    }

    if (gnss) {
        add(Group::Gnss, "fixes", link::diagnostics_key::kFixes, Kind::Unsigned,
            diagnostics.gnss_fixes);
        add(Group::Gnss, "valid", link::diagnostics_key::kValid, Kind::Boolean,
            diagnostics.fix_valid ? 1 : 0);
        // A receiver silently at another rate is a GNSS-less device that looks
        // fitted, and it is the top support case this part will generate.
        add(Group::Gnss, "baud", link::diagnostics_key::kBaud, Kind::Unsigned,
            diagnostics.gnss_baud);
        add(Group::Gnss, "config", link::diagnostics_key::kConfig, Kind::Code,
            static_cast<int64_t>(diagnostics.gnss_config),
            ports::to_string(diagnostics.gnss_config));
        add(Group::Gnss, "raises", link::diagnostics_key::kRaises, Kind::Unsigned,
            diagnostics.gnss_raises);
        add(Group::Gnss, "raise_fallbacks", link::diagnostics_key::kRaiseFallbacks, Kind::Unsigned,
            diagnostics.gnss_raise_fallbacks);
        // INFO: fc 19sep26 the phase a solution lands at, which kFixMaxAgeMs refuses a burst over
        if (diagnostics.gnss_solution_phase_valid)
            add(Group::Gnss, "solution_phase_ms", link::diagnostics_key::kSolutionPhaseMs,
                Kind::Unsigned, diagnostics.gnss_solution_phase_ms);
        add(Group::Gnss, "overruns", link::diagnostics_key::kOverruns, Kind::Unsigned,
            diagnostics.gnss_overruns);
        add(Group::Gnss, "identified", link::diagnostics_key::kIdentified, Kind::Boolean,
            diagnostics.gnss_identified ? 1 : 0);
        add(Group::Gnss, "firmware", link::diagnostics_key::kFirmware, Kind::Text, 0,
            diagnostics.gnss_firmware);
        add(Group::Gnss, "reject", link::diagnostics_key::kReject, Kind::Code,
            static_cast<int64_t>(diagnostics.gnss_reject),
            gnss::reject_name(diagnostics.gnss_reject));
        add(Group::Gnss, "rejected", link::diagnostics_key::kRejected, Kind::Unsigned,
            diagnostics.gnss_rejected);
        // Absent rather than zero: a device with no two consecutive fixes has no
        // residual, and 0 m is what a perfect one reports.
        if (diagnostics.residual_valid)
            add(Group::Gnss, "residual_m", link::diagnostics_key::kResidualM, Kind::Unsigned,
                diagnostics.residual_m);
    }

    if (power) {
        add(Group::Power, "cell_mv", link::diagnostics_key::kCellMv, Kind::Unsigned,
            diagnostics.battery.cell_mv);
        add(Group::Power, "percent", link::diagnostics_key::kPercent, Kind::Unsigned,
            diagnostics.battery.percent);
        add(Group::Power, "battery_valid", link::diagnostics_key::kBatteryValid, Kind::Boolean,
            diagnostics.battery.valid ? 1 : 0);
        add(Group::Power, "charging", link::diagnostics_key::kCharging, Kind::Boolean,
            diagnostics.battery.charging ? 1 : 0);
        add(Group::Power, "power_level", link::diagnostics_key::kPowerLevel, Kind::Code,
            static_cast<int64_t>(diagnostics.power_level),
            power::to_string(diagnostics.power_level));
        add(Group::Power, "supply_warnings", link::diagnostics_key::kSupplyWarnings, Kind::Unsigned,
            diagnostics.supply_warnings);
        add(Group::Power, "implausible", link::diagnostics_key::kImplausible, Kind::Unsigned,
            diagnostics.battery_implausible);
        // Same rule as the status reply, and the same key: no reading is no key,
        // never a zero, because 0.0 C is a plausible hangar morning.
        if (diagnostics.die_valid)
            add(Group::Power, "die_temperature_c", link::diagnostics_key::kDieTemperatureC,
                Kind::Integer, whole_celsius(diagnostics.die_dc));
        if (diagnostics.charge != power::ChargeCondition::Unknown)
            add(Group::Power, "charge", link::diagnostics_key::kCharge, Kind::Code,
                static_cast<int64_t>(diagnostics.charge), power::to_string(diagnostics.charge));
        add(Group::Power, "charge_warnings", link::diagnostics_key::kChargeWarnings, Kind::Unsigned,
            diagnostics.charge_warnings);
    }
}

void DiagnosticsReport::add(Group group, const char* name, uint32_t key, Kind kind, int64_t value,
                            const char* text) {
    if (count_ >= kMaxFields) return;
    fields_[count_++] = Field{name, key, text, value, kind, group};
    if (count_ == 1 || fields_[count_ - 2].group != group) line_count_++;
}

const char* DiagnosticsReport::group_name(Group group) {
    switch (group) {
        case Group::System: return "system";
        case Group::Radio: return "radio";
        case Group::Traffic: return "traffic";
        case Group::Gnss: return "gnss";
        case Group::Power: return "power";
    }
    return "?";
}

int DiagnosticsReport::field_bytes(const Field& field) {
    switch (field.kind) {
        case Kind::Unsigned:
        case Kind::Code:
            return cbor::unsigned_entry_bytes(field.key, static_cast<uint64_t>(field.value));
        case Kind::Integer: return cbor::integer_entry_bytes(field.key, field.value);
        case Kind::Boolean: return cbor::boolean_entry_bytes(field.key);
        case Kind::Text: return cbor::text_entry_bytes(field.key, field.text);
    }
    return 0;
}

int DiagnosticsReport::line_start(int index) const {
    if (index < 0) return count_;
    int at = 0;
    for (int line = 0; at < count_; line++) {
        if (line == index) return at;
        const Group group = fields_[at].group;
        while (at < count_ && fields_[at].group == group) at++;
    }
    return count_;
}

int DiagnosticsReport::line(int index, char* out, int capacity) const {
    const int start = line_start(index);
    if (start >= count_) return 0;
    const Group group = fields_[start].group;

    int end = start;
    int need = cbor::text_bytes(group_name(group));
    while (end < count_ && fields_[end].group == group) {
        const Field& field = fields_[end];
        // INFO: fc 20oct26 " key=" and the value, text quoted: a reason with a space is one field
        need += 2 + cbor::text_bytes(field.name);
        switch (field.kind) {
            case Kind::Unsigned:
            case Kind::Integer: need += number_bytes(field.value); break;
            case Kind::Boolean: need += cbor::text_bytes(boolean_text(field.value != 0)); break;
            case Kind::Text:
            case Kind::Code: need += cbor::text_bytes(field.text) + 2; break;
        }
        end++;
    }
    if (need + 1 > capacity) return 0;

    int written = write_text(out, group_name(group));
    for (int i = start; i < end; i++) {
        const Field& field = fields_[i];
        out[written++] = ' ';
        written += write_text(out + written, field.name);
        out[written++] = '=';
        switch (field.kind) {
            case Kind::Unsigned:
            case Kind::Integer: written += write_number(out + written, field.value); break;
            case Kind::Boolean:
                written += write_text(out + written, boolean_text(field.value != 0));
                break;
            case Kind::Text:
            case Kind::Code:
                out[written++] = '"';
                written += write_text(out + written, field.text);
                out[written++] = '"';
                break;
        }
    }
    out[written] = 0;
    return written;
}

// Asked before the first frame goes out, so the answer is all or nothing: a bench
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
            case Kind::Code:
                writer.add_unsigned(field.key, static_cast<uint64_t>(field.value));
                break;
            case Kind::Integer: writer.add_integer(field.key, field.value); break;
            case Kind::Boolean: writer.add_boolean(field.key, field.value != 0); break;
            case Kind::Text: writer.add_text(field.key, field.text); break;
        }
    }
    const int bytes = writer.finish();
    if (bytes == 0) return 0;

    at_ += packed;
    part_++;
    return bytes;
}

}  // namespace skyblip::comms
