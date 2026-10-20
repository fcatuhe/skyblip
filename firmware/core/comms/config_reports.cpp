#include "core/comms/config.h"
#include "core/comms/timing_report.h"
#include "core/dfu/image.h"
#include "core/util/cbor.h"
#include "core/util/span.h"
#include "ports/link.h"

namespace skyblip::comms {

namespace {
constexpr int kFlashFrameBytes = 64;
constexpr size_t kKeyPrefixBytes = 4;
}

// INFO: fc 04aug26 pushed unsolicited, so it carries state only and fits at its worst: README.md
int ConfigService::format_status(uint8_t* out, int capacity) {
    cbor::Writer writer(out, capacity);
    writer.add_code(link::kCommandKey, link::Command::Status);
    writer.add_code(link::status_key::kFlight, flight_state_);
    writer.add_boolean(link::status_key::kUpload, upload_allowed());
    const power::BatteryState& battery = diagnostics_.battery;
    if (battery.valid) writer.add_unsigned(link::status_key::kBatteryPercent, battery.percent);
    writer.add_boolean(link::status_key::kCharging, battery.charging);
    writer.add_code(link::status_key::kPowerLevel, diagnostics_.power_level);
    writer.add_boolean(link::status_key::kWentDarkFlat, went_dark_flat_);
    if (diagnostics_.die_valid)
        writer.add_integer(link::status_key::kDieTemperatureC, whole_celsius(diagnostics_.die_dc));
    return writer.finish();
}

void ConfigService::send_status() {
    uint8_t frame[kSmallestSupportedPayloadBytes];
    const int bytes = format_status(frame, static_cast<int>(sizeof(frame)));
    if (bytes == 0) {
        diagnostics_.link_drops++;
        status_push_due_ = false;
        return;
    }
    (void)reply(frame, bytes);
}

void ConfigService::send_defaults() {
    uint8_t frame[kSmallestSupportedPayloadBytes];
    cbor::Writer writer(frame, sizeof(frame));
    writer.add_code(link::kCommandKey, link::Command::Defaults);
    store_.write_default_fields(writer);
    const int bytes = writer.finish();
    if (bytes == 0) {
        diagnostics_.link_drops++;
        return;
    }
    (void)reply(frame, bytes);
}

// INFO: fc 18sep26 Nobody asked for this one, so every app subscribed to it gets it.
void ConfigService::push_status() {
    uint8_t frame[kSmallestSupportedPayloadBytes];
    const int bytes = format_status(frame, static_cast<int>(sizeof(frame)));
    if (bytes == 0) {
        diagnostics_.link_drops++;
        status_push_due_ = false;
        return;
    }
    status_push_due_ = broadcast(frame, bytes) == Status::WouldBlock;
}

// INFO: fc 04aug26 The one sender allowed more than one frame, and the reason
// lives in core/comms/timing_report.h: a laboratory reads this once, so a
// two-frame answer is free, while trimming a histogram would destroy the
// evidence it was asked for. A frame that cannot be produced at all is counted
// and the rest of the report is abandoned rather than sent with a hole in it.
void ConfigService::send_timing() {
    if (timing_statistics_ == nullptr) {
        acknowledge(false, link::Reason::NoStatistics);
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
        acknowledge(false, link::Reason::NoStatistics);
        return;
    }
    uint8_t frame[kFlashFrameBytes];
    cbor::Writer writer(frame, sizeof(frame));
    writer.add_code(link::kCommandKey, link::Command::Flash);
    writer.add_boolean(link::flash_key::kWritable, settings_writable());
    writer.add_unsigned(link::flash_key::kRequests, durable_writes_->requests());
    writer.add_unsigned(link::flash_key::kWrites, durable_writes_->writes());
    writer.add_unsigned(link::flash_key::kForced, durable_writes_->forced());
    writer.add_unsigned(link::flash_key::kWorstWaitMs, durable_writes_->worst_wait_ms());
    writer.add_boolean(link::flash_key::kPending, durable_writes_->pending());
    writer.add_unsigned(link::flash_key::kBudgetMs, timing::DurableWriteWindow::kWorstWriteMs);
    writer.add_unsigned(link::flash_key::kBoundMs, timing::DurableWriteWindow::kDeferCeilingMs);
    (void)reply(frame, writer.finish());
}

// The radio's half of the same bench, still its own question rather than four
// more keys on "status": that reply is the one this service PUSHES unsolicited
// and it is already sized against the narrowest phone in the field at its worst
// case. A status push that vanished whenever a unit was hot AND refusing packets
// would be the exact failure the payload ceiling exists to prevent.
void ConfigService::send_radio() {
    start_report(diagnostics_report_, DiagnosticsReport::kFrameBytes, diagnostics_,
                 link::Command::Radio, DiagnosticsReport::Group::Radio);
}

void ConfigService::send_update(uint16_t session_id) {
    char from[dfu::kVersionTextBytes];
    char to[dfu::kVersionTextBytes];
    dfu::format_version(update_record_.from, from, sizeof(from));
    dfu::format_version(update_record_.to, to, sizeof(to));
    uint8_t frame[kSmallestSupportedPayloadBytes];
    cbor::Writer writer(frame, sizeof(frame));
    writer.add_code(link::kCommandKey, link::Command::Update);
    writer.add_code(link::update_key::kImage, image_state_);
    if (image_state_ != dfu::ImageState::Confirmed) {
        writer.add_text(link::update_key::kFrom, from);
        writer.add_text(link::update_key::kTo, to);
    }
    if (settings_fallback_ != settings::Fallback::None)
        writer.add_code(link::update_key::kSettings, settings_fallback_);
    writer.add_boolean(link::update_key::kSwapPowered, swap_powered());
    writer.add_code(link::update_key::kImu, hub_image_.holding);
    if (hub_image_.holding == dfu::HubImage::Held)
        writer.add_bytes(link::update_key::kImuDigest,
                         ConstByteSpan(hub_image_.digest, dfu::HubImageReport::kDigestBytes));
    if (dfu_ != nullptr && dfu_->downgrade_allowed())
        writer.add_boolean(link::update_key::kDowngrade, true);
    ports::SigningKeyHash trusted;
    if (dfu_ != nullptr && dfu_->running_key(trusted))
        writer.add_bytes(link::update_key::kKey, ConstByteSpan(trusted.data(), kKeyPrefixBytes));
    const int bytes = writer.finish();
    if (bytes == 0) {
        diagnostics_.link_drops++;
        return;
    }
    (void)reply_to(session_id, frame, bytes);
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
        acknowledge(false, link::Reason::NoStatistics);
        return;
    }
    start_report(diagnostics_report_, DiagnosticsReport::kFrameBytes, diagnostics_,
                 link::Command::Diagnostics);
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
    uint8_t frame[kHeldFrameBytes];
    while (report && held_bytes_ == 0) {
        const int bytes = report->next_frame(report_payload_bytes_, frame, report_frame_bytes_);
        if (bytes <= 0) {
            diagnostics_.link_drops++;
            report.reset();
            return;
        }
        const Status sent = reply(frame, bytes);
        if (report->exhausted() || (sent != Status::Ok && sent != Status::WouldBlock))
            report.reset();
    }
}

void ConfigService::resume_replies(uint32_t now_ms) {
    now_ms_ = now_ms;
    if (held_bytes_ > 0) {
        const Status sent =
            link_.send_to(held_for_session_id_, events::Endpoint::Config,
                          ConstByteSpan(held_frame_, static_cast<size_t>(held_bytes_)));
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
