#include "core/comms/diagnostics.h"

#include <algorithm>

#include "core/comms/frame_budget.h"
#include "core/util/json.h"

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

int write_long(char* out, long value) {
    const int written = frame::number_bytes(value);
    unsigned long magnitude =
        value < 0 ? static_cast<unsigned long>(-value) : static_cast<unsigned long>(value);
    int at = written;
    do {
        out[--at] = static_cast<char>('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude != 0);
    if (value < 0) out[0] = '-';
    return written;
}

const char* boolean_text(bool value) { return value ? "true" : "false"; }

// The saturating counter both link reports share, and why they have to
// (core/comms/frame_budget.h).
using frame::counter;

}  // namespace

int whole_celsius(int16_t temperature_dc) {
    const int16_t bias = temperature_dc >= 0 ? 5 : -5;
    return (temperature_dc + bias) / 10;
}

DiagnosticsReport::DiagnosticsReport(const Diagnostics& diagnostics, const char* command)
    : command_(command) {
    build(diagnostics, nullptr);
}

DiagnosticsReport::DiagnosticsReport(const Diagnostics& diagnostics, const char* command,
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
        add_integer(Group::System, "uptime_s", counter(diagnostics.uptime_s));
        add_string(Group::System, "reset", power::to_string(diagnostics.reset));
        add_integer(Group::System, "link_drops", counter(diagnostics.link_drops));
    }

    if (radio) {
        add_integer(Group::Radio, "noise_dbm", diagnostics.noise_dbm);
        add_integer(Group::Radio, "duty_permille", counter(diagnostics.duty_permille));
        add_integer(Group::Radio, "rx_ok", counter(diagnostics.rx_ok));
        add_integer(Group::Radio, "rx_bad", counter(diagnostics.rx_bad));
        add_integer(Group::Radio, "rx_wait", counter(diagnostics.rx_wait));
        add_integer(Group::Radio, "rx_type", counter(diagnostics.rx_type));
        add_integer(Group::Radio, "rx_unframed", counter(diagnostics.rx_unframed));
        add_integer(Group::Radio, "rx_miskeyed", counter(diagnostics.rx_miskeyed));
        add_integer(Group::Radio, "rx_noise", counter(diagnostics.rx_noise));
        add_integer(Group::Radio, "rx_named", counter(diagnostics.rx_named));
        add_integer(Group::Radio, "tx_ok", counter(diagnostics.tx_ok));
        add_integer(Group::Radio, "tx_lost", counter(diagnostics.tx_lost));
        add_integer(Group::Radio, "tx_named", counter(diagnostics.tx_named));
        add_integer(Group::Radio, "tx_keyed_us", diagnostics.tx_keyed_us);
        add_integer(Group::Radio, "tx_span_us", diagnostics.tx_span_us);
        // The range gate's refusals read out with the radio and not with the
        // table, because what they measure is this receiver's link budget: a rate
        // that climbs is a decoder correcting a frame into a position no radio
        // this size could have heard (core/traffic/sanity.h).
        add_integer(Group::Radio, "range_refused", counter(diagnostics.range_refused));
        add_boolean(Group::Radio, "dio1_armed", diagnostics.dio1_armed);
        add_integer(Group::Radio, "dio1_edges", counter(diagnostics.dio1_edges));
        add_integer(Group::Radio, "dio1_missed", counter(diagnostics.dio1_missed));
    }

    if (traffic) {
        add_integer(Group::Traffic, "tracked", counter(diagnostics.tracked));
        add_integer(Group::Traffic, "alarm", diagnostics.alarm);
    }

    if (gnss) {
        add_integer(Group::Gnss, "fixes", counter(diagnostics.gnss_fixes));
        add_boolean(Group::Gnss, "valid", diagnostics.fix_valid);
        // A receiver silently at another rate is a GNSS-less device that looks
        // fitted, and it is the top support case this part will generate.
        add_integer(Group::Gnss, "baud", counter(diagnostics.gnss_baud));
        add_string(Group::Gnss, "config", ports::to_string(diagnostics.gnss_config));
        add_integer(Group::Gnss, "raises", counter(diagnostics.gnss_raises));
        add_integer(Group::Gnss, "raise_fallbacks", counter(diagnostics.gnss_raise_fallbacks));
        // INFO: fc 19sep26 the phase a solution lands at, which kFixMaxAgeMs refuses a burst over
        if (diagnostics.gnss_solution_phase_valid)
            add_integer(Group::Gnss, "solution_phase_ms", diagnostics.gnss_solution_phase_ms);
        add_integer(Group::Gnss, "overruns", counter(diagnostics.gnss_overruns));
        add_boolean(Group::Gnss, "identified", diagnostics.gnss_identified);
        add_string(Group::Gnss, "firmware", diagnostics.gnss_firmware);
        add_string(Group::Gnss, "reject", gnss::reject_name(diagnostics.gnss_reject));
        add_integer(Group::Gnss, "rejected", counter(diagnostics.gnss_rejected));
        // Absent rather than zero: a device with no two consecutive fixes has no
        // residual, and 0 m is what a perfect one reports.
        if (diagnostics.residual_valid)
            add_integer(Group::Gnss, "residual_m", diagnostics.residual_m);
    }

    if (power) {
        add_integer(Group::Power, "cell_mv", diagnostics.battery.cell_mv);
        add_integer(Group::Power, "percent", diagnostics.battery.percent);
        add_boolean(Group::Power, "battery_valid", diagnostics.battery.valid);
        add_boolean(Group::Power, "charging", diagnostics.battery.charging);
        add_string(Group::Power, "power_level", power::to_string(diagnostics.power_level));
        add_integer(Group::Power, "supply_warnings", counter(diagnostics.supply_warnings));
        add_integer(Group::Power, "implausible", counter(diagnostics.battery_implausible));
        // Same rule as the status reply, and the same key: no reading is no key,
        // never a zero, because 0.0 C is a plausible hangar morning.
        if (diagnostics.die_valid)
            add_integer(Group::Power, "die_temperature_c", whole_celsius(diagnostics.die_dc));
        if (diagnostics.charge != power::ChargeCondition::Unknown)
            add_string(Group::Power, "charge", power::to_string(diagnostics.charge));
        add_integer(Group::Power, "charge_warnings", counter(diagnostics.charge_warnings));
    }
}

void DiagnosticsReport::add_integer(Group group, const char* key, long value) {
    if (count_ >= kMaxFields) return;
    fields_[count_++] = Field{key, nullptr, value, Kind::Integer, group};
    if (count_ == 1 || fields_[count_ - 2].group != group) line_count_++;
}

void DiagnosticsReport::add_boolean(Group group, const char* key, bool value) {
    if (count_ >= kMaxFields) return;
    fields_[count_++] = Field{key, nullptr, value ? 1 : 0, Kind::Boolean, group};
    if (count_ == 1 || fields_[count_ - 2].group != group) line_count_++;
}

void DiagnosticsReport::add_string(Group group, const char* key, const char* value) {
    if (count_ >= kMaxFields) return;
    fields_[count_++] = Field{key, value, 0, Kind::String, group};
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
        case Kind::Integer: return frame::integer_field_bytes(field.key, field.value);
        case Kind::Boolean: return frame::boolean_field_bytes(field.key);
        case Kind::String: return frame::string_field_bytes(field.key, field.text);
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
    int need = frame::text_bytes(group_name(group));
    while (end < count_ && fields_[end].group == group) {
        const Field& field = fields_[end];
        // " key=" plus the value, which is quoted when it is text so a reset
        // reason with a space in it stays one field on a line and one value in the
        // JSON.
        need += 2 + frame::text_bytes(field.key);
        switch (field.kind) {
            case Kind::Integer: need += frame::number_bytes(field.value); break;
            case Kind::Boolean: need += frame::text_bytes(boolean_text(field.value != 0)); break;
            case Kind::String: need += frame::text_bytes(field.text) + 2; break;
        }
        end++;
    }
    if (need + 1 > capacity) return 0;

    int written = write_text(out, group_name(group));
    for (int i = start; i < end; i++) {
        const Field& field = fields_[i];
        out[written++] = ' ';
        written += write_text(out + written, field.key);
        out[written++] = '=';
        switch (field.kind) {
            case Kind::Integer: written += write_long(out + written, field.value); break;
            case Kind::Boolean:
                written += write_text(out + written, boolean_text(field.value != 0));
                break;
            case Kind::String:
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
    int widest_group = 0;
    for (int i = 0; i < count_; i++) {
        const int bytes = field_bytes(fields_[i]);
        widest_field = std::max(bytes, widest_field);
        const int group = frame::string_field_bytes("group", group_name(fields_[i].group));
        widest_group = std::max(group, widest_group);
    }
    frame::Budget budget(payload_bytes);
    return budget.take(frame::string_field_bytes("command", command_)) &&
           budget.take(widest_group) && budget.take(frame::integer_field_bytes("part", count_)) &&
           budget.take(frame::kMoreFieldBytes) && budget.take(widest_field);
}

int DiagnosticsReport::next_frame(int payload_bytes, char* out, int capacity) {
    if (exhausted()) return 0;

    int room = payload_bytes + 1;
    room = std::min(room, capacity);

    const Group group = fields_[at_].group;
    frame::Budget budget(room - 1);
    budget.take(frame::string_field_bytes("command", command_));
    budget.take(frame::string_field_bytes("group", group_name(group)));
    budget.take(frame::integer_field_bytes("part", part_));
    budget.take(frame::kMoreFieldBytes);
    int packed = 0;
    while (at_ + packed < count_ && fields_[at_ + packed].group == group &&
           budget.take(field_bytes(fields_[at_ + packed]))) {
        packed++;
    }
    if (packed == 0) return 0;

    json::Writer writer(out, room);
    writer.add_string("command", command_);
    writer.add_string("group", group_name(group));
    writer.add_integer("part", part_);
    writer.add_boolean("more", at_ + packed < count_);
    for (int i = 0; i < packed; i++) {
        const Field& field = fields_[at_ + i];
        switch (field.kind) {
            case Kind::Integer: writer.add_integer(field.key, field.value); break;
            case Kind::Boolean: writer.add_boolean(field.key, field.value != 0); break;
            case Kind::String: writer.add_string(field.key, field.text); break;
        }
    }
    const int bytes = writer.finish();
    if (writer.overflowed()) return 0;

    at_ += packed;
    part_++;
    return bytes;
}

}  // namespace skyblip::comms
