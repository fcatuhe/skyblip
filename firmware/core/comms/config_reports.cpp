#include "core/comms/config.h"
#include "core/comms/timing_report.h"
#include "core/dfu/image.h"
#include "core/util/format.h"
#include "core/util/json.h"
#include "core/util/span.h"
#include "ports/link.h"

namespace skyblip::comms {

// INFO: fc 04aug26 pushed unsolicited, so it carries state only and fits at its worst: README.md
int ConfigService::format_status(char* out, int capacity) {
    json::Writer writer(out, capacity);
    writer.add_string("command", "status");
    writer.add_string("flight", flight_name(flight_state_));
    writer.add_boolean("upload", upload_allowed());
    const power::BatteryState& battery = diagnostics_.battery;
    if (battery.valid) writer.add_integer("battery_percent", static_cast<long>(battery.percent));
    writer.add_boolean("charging", battery.charging);
    writer.add_string("power_level", power::to_string(diagnostics_.level));
    writer.add_boolean("went_dark_flat", went_dark_flat_);
    if (diagnostics_.die_valid)
        writer.add_integer("die_temperature_c", whole_celsius(diagnostics_.die_dc));
    const int bytes = writer.finish();
    return writer.overflowed() ? 0 : bytes;
}

void ConfigService::send_status() {
    char buffer[kSmallestSupportedPayloadBytes + 1];
    const int bytes = format_status(buffer, static_cast<int>(sizeof(buffer)));
    if (bytes == 0) {
        diagnostics_.link_drops++;
        status_push_due_ = false;
        return;
    }
    (void)reply(buffer, bytes);
}

void ConfigService::send_defaults() {
    char buffer[kSmallestSupportedPayloadBytes + 1];
    json::Writer writer(buffer, sizeof(buffer));
    writer.add_string("command", "defaults");
    store_.write_default_fields(writer);
    const int bytes = writer.finish();
    if (writer.overflowed()) {
        diagnostics_.link_drops++;
        return;
    }
    (void)reply(buffer, bytes);
}

// INFO: fc 18sep26 Nobody asked for this one, so every app subscribed to it gets it.
void ConfigService::push_status() {
    char buffer[kSmallestSupportedPayloadBytes + 1];
    const int bytes = format_status(buffer, static_cast<int>(sizeof(buffer)));
    if (bytes == 0) {
        diagnostics_.link_drops++;
        status_push_due_ = false;
        return;
    }
    status_push_due_ = broadcast(buffer, bytes) == Status::WouldBlock;
}

// INFO: fc 04aug26 The one sender allowed more than one frame, and the reason
// lives in core/comms/timing_report.h: a laboratory reads this once, so a
// two-frame answer is free, while trimming a histogram would destroy the
// evidence it was asked for. A frame that cannot be produced at all is counted
// and the rest of the report is abandoned rather than sent with a hole in it.
void ConfigService::send_timing() {
    if (timing_statistics_ == nullptr) {
        acknowledge(false, "no_statistics");
        return;
    }
    start_report(timing_report_, kTimingFrameBytes, *timing_statistics_);
}

// The durable-write half of the same bench: the settings writes the device made,
// the changes they were coalesced from, and the ones the policy could not place
// inside a free phase of the second. `forced` is the only number here that is a
// fault, and it is the reason this is exported at all - a stall that landed where
// the slot map did not budget for one must not be silent.
//
// Deliberately its own object rather than four more keys on "timing": that reply
// is already the longest thing this service sends, and the link negotiates a
// notification size it has to fit inside. Same endpoint, same dispatch, one more
// question - not a longer answer a phone might truncate.
void ConfigService::send_flash() {
    if (durable_writes_ == nullptr) {
        acknowledge(false, "no_statistics");
        return;
    }
    char buffer[192];
    json::Writer writer(buffer, sizeof(buffer));
    writer.add_string("command", "flash");
    writer.add_boolean("writable", settings_writable());
    writer.add_integer("requests", static_cast<long>(durable_writes_->requests()));
    writer.add_integer("writes", static_cast<long>(durable_writes_->writes()));
    writer.add_integer("forced", static_cast<long>(durable_writes_->forced()));
    writer.add_integer("worst_wait_ms", static_cast<long>(durable_writes_->worst_wait_ms()));
    writer.add_boolean("pending", durable_writes_->pending());
    writer.add_integer("budget_ms", static_cast<long>(timing::DurableWriteWindow::kWorstWriteMs));
    writer.add_integer("bound_ms", static_cast<long>(timing::DurableWriteWindow::kDeferCeilingMs));
    writer.finish();
    (void)reply(buffer);
}

// The radio's half of the same bench, still its own question rather than four
// more keys on "status": that reply is the one this service PUSHES unsolicited
// and it is already sized against the narrowest phone in the field at its worst
// case. A status push that vanished whenever a unit was hot AND refusing packets
// would be the exact failure the payload ceiling exists to prevent.
void ConfigService::send_radio() {
    start_report(diagnostics_report_, DiagnosticsReport::kFrameBytes, diagnostics_, "radio",
                 DiagnosticsReport::Group::Radio);
}

void ConfigService::send_update(uint16_t session_id) {
    char from[dfu::kVersionTextBytes];
    char to[dfu::kVersionTextBytes];
    char hub_image[dfu::kHubImageTextBytes];
    dfu::format_version(update_record_.from, from, sizeof(from));
    dfu::format_version(update_record_.to, to, sizeof(to));
    dfu::format_hub_image(hub_image_, hub_image, sizeof(hub_image));
    char buffer[kSmallestSupportedPayloadBytes + 1];
    json::Writer writer(buffer, sizeof(buffer));
    writer.add_string("command", "update");
    writer.add_string("image", dfu::to_string(image_state_));
    if (image_state_ != dfu::ImageState::Confirmed) {
        writer.add_string("from", from);
        writer.add_string("to", to);
    }
    if (settings_fallback_ != settings::Fallback::None)
        writer.add_string("settings", settings::to_string(settings_fallback_));
    writer.add_boolean("swap_powered", swap_powered());
    writer.add_string("imu", hub_image);
    if (dfu_ != nullptr && dfu_->downgrade_allowed()) writer.add_boolean("downgrade", true);
    ports::SigningKeyHash trusted;
    if (dfu_ != nullptr && dfu_->running_key(trusted)) {
        char key[9];
        key[format_hex(key, dfu::key_prefix(trusted), 8)] = 0;
        writer.add_string("key", key);
    }
    const int bytes = writer.finish();
    if (writer.overflowed()) {
        diagnostics_.link_drops++;
        return;
    }
    (void)reply_to(session_id, buffer, bytes);
}

// A whole dump nobody has collected is a dump of zeros, and zeros here read as a
// receiver that has never seen a satellite and a supply that has never faltered -
// which is a device reporting health it has not measured. So it is refused with a
// reason, exactly as an unwired timing accumulator is. The radio group above is not
// gated: it predates the collector, its own key is written every pass by whoever
// owns the table (set_range_refused), and a radio that has heard nothing truthfully
// has zero receptions.
void ConfigService::send_diagnostics() {
    if (diagnostics_.refreshes == 0) {
        acknowledge(false, "no_statistics");
        return;
    }
    start_report(diagnostics_report_, DiagnosticsReport::kFrameBytes, diagnostics_, "diagnostics");
}

template <class Report, class... Arguments>
void ConfigService::start_report(std::optional<Report>& report, int frame_bytes,
                                 const Arguments&... arguments) {
    report_payload_bytes_ = payload_bytes_to(claim_.holder());
    report_frame_bytes_ = frame_bytes;
    if (!report.emplace(arguments...).fits(report_payload_bytes_)) {
        diagnostics_.link_drops++;
        report.reset();
        return;
    }
    continue_report(report);
}

// INFO: fc 25sep26 a frame held for the link still counts as sent, so the next one follows it
template <class Report>
void ConfigService::continue_report(std::optional<Report>& report) {
    char buffer[kHeldFrameBytes];
    while (report && held_bytes_ == 0) {
        const int bytes = report->next_frame(report_payload_bytes_, buffer, report_frame_bytes_);
        if (bytes <= 0) {
            diagnostics_.link_drops++;
            report.reset();
            return;
        }
        const Status sent = reply(buffer, bytes);
        if (report->exhausted() || (sent != Status::Ok && sent != Status::WouldBlock))
            report.reset();
    }
}

void ConfigService::resume_replies(uint32_t now_ms) {
    now_ms_ = now_ms;
    if (held_bytes_ > 0) {
        const Status sent =
            link_.send_to(held_for_session_id_, events::Endpoint::Config,
                          ConstByteSpan(reinterpret_cast<const uint8_t*>(held_frame_),
                                        static_cast<size_t>(held_bytes_)));
        if (sent == Status::WouldBlock && now_ms - held_since_ms_ < ports::kReplyWaitCeilingMs)
            return;
        held_bytes_ = 0;
        if (!is_ok(sent)) {
            diagnostics_.link_drops++;
            drop_replies();
            return;
        }
    }
    continue_report(diagnostics_report_);
    continue_report(timing_report_);
}

}  // namespace skyblip::comms
