#include "core/comms/config.h"

#include <cstring>

#include "core/util/cbor.h"
#include "core/util/span.h"

namespace skyblip::comms {

namespace {
constexpr int kAcknowledgementFrameBytes = 16;
static_assert(
    kAcknowledgementFrameBytes <= ports::kMinimumLinkPayloadBytes,
    "every acknowledgement, small_mtu above all, fits a link that never exchanged its MTU");
}  // namespace

bool ConfigChannel::connected(uint16_t session_id) const {
    for (int i = 0; i < connected_count_; i++)
        if (connected_sessions_[i] == session_id) return true;
    return false;
}

void ConfigChannel::connect(uint16_t session_id) {
    if (connected(session_id) || connected_count_ == static_cast<int>(LinkSessions::kMaxSessions))
        return;
    connected_sessions_[connected_count_++] = session_id;
}

bool ConfigChannel::disconnect(uint16_t session_id) {
    for (int i = 0; i < connected_count_; i++) {
        if (connected_sessions_[i] != session_id) continue;
        connected_sessions_[i] = connected_sessions_[--connected_count_];
        break;
    }
    if (!claim_.holds(session_id)) return false;
    claim_.release();
    drop_replies();
    return true;
}

bool ConfigChannel::admit(const events::Peer& peer) {
    if (claim_.grant(peer)) return true;
    refuse_unclaimed(peer);
    return false;
}

Status ConfigChannel::broadcast(const uint8_t* frame, int bytes) {
    if (bytes <= 0 || bytes > payload_bytes()) {
        link_drops_++;
        return Status::OutOfRange;
    }
    const Status sent =
        link_.send(events::Endpoint::Config, ConstByteSpan(frame, static_cast<size_t>(bytes)));
    if (!is_ok(sent)) link_drops_++;
    return sent;
}

// INFO: fc 04aug26 the controller fails a frame wider than the payload, so count it and keep it
Status ConfigChannel::reply(const uint8_t* frame, int bytes) {
    return reply_to(claim_.peer(), frame, bytes);
}

Status ConfigChannel::reply_to(const events::Peer& peer, const uint8_t* frame, int bytes) {
    const Status sent = deliver(peer, frame, bytes);
    if (sent == Status::OutOfRange && bytes > 0 && claim_.holds(peer)) refuse_too_wide();
    return sent;
}

// INFO: fc 25sep26 one frame waits for the link, and a second one while it does is dropped as Full
Status ConfigChannel::deliver(const events::Peer& peer, const uint8_t* frame, int bytes) {
    if (bytes <= 0 || bytes > payload_bytes_to(peer.session_id) || held_bytes_ > 0) {
        link_drops_++;
        return held_bytes_ > 0 ? Status::Full : Status::OutOfRange;
    }
    const Status sent = link_.send_to(peer, events::Endpoint::Config,
                                      ConstByteSpan(frame, static_cast<size_t>(bytes)));
    if (sent == Status::WouldBlock)
        hold(peer, frame, bytes);
    else if (!is_ok(sent))
        link_drops_++;
    return sent;
}

void ConfigChannel::hold(const events::Peer& peer, const uint8_t* frame, int bytes) {
    if (bytes > kHeldFrameBytes) {
        link_drops_++;
        return;
    }
    std::memcpy(held_frame_, frame, static_cast<size_t>(bytes));
    held_bytes_ = bytes;
    held_for_ = peer;
    held_since_ms_ = now_ms_;
}

void ConfigChannel::drop_replies() {
    held_bytes_ = 0;
    diagnostics_report_.reset();
    timing_report_.reset();
}

void ConfigChannel::refuse_unclaimed(const events::Peer& peer) {
    uint8_t frame[kAcknowledgementFrameBytes];
    cbor::Writer writer(frame, sizeof(frame));
    writer.add_code(link::kCommandKey, link::Command::Acknowledgement);
    writer.add_boolean(link::acknowledgement_key::kAcknowledge, false);
    writer.add_code(link::acknowledgement_key::kReason, link::Reason::Claimed);
    writer.add_unsigned(link::acknowledgement_key::kBy, claim_.holder());
    (void)reply_to(peer, frame, writer.finish());
}

void ConfigChannel::acknowledge_pending(link::Reason reason) {
    uint8_t frame[kAcknowledgementFrameBytes];
    cbor::Writer writer(frame, sizeof(frame));
    writer.add_code(link::kCommandKey, link::Command::Acknowledgement);
    writer.add_boolean(link::acknowledgement_key::kAcknowledge, false);
    writer.add_boolean(link::acknowledgement_key::kPending, true);
    writer.add_code(link::acknowledgement_key::kReason, reason);
    (void)reply(frame, writer.finish());
}

void ConfigChannel::acknowledge(bool accepted, std::optional<link::Reason> reason) {
    acknowledge_to(claim_.peer(), accepted, reason);
}

void ConfigChannel::acknowledge_to(const events::Peer& peer, bool accepted,
                                   std::optional<link::Reason> reason) {
    uint8_t frame[kAcknowledgementFrameBytes];
    cbor::Writer writer(frame, sizeof(frame));
    writer.add_code(link::kCommandKey, link::Command::Acknowledgement);
    writer.add_boolean(link::acknowledgement_key::kAcknowledge, accepted);
    if (reason) writer.add_code(link::acknowledgement_key::kReason, *reason);
    (void)reply_to(peer, frame, writer.finish());
}

void ConfigChannel::start_diagnostics(const Diagnostics& diagnostics, link::Command command) {
    start_report(diagnostics_report_, DiagnosticsReport::kFrameBytes, diagnostics, command);
}

void ConfigChannel::start_diagnostics(const Diagnostics& diagnostics, link::Command command,
                                      DiagnosticsReport::Group only) {
    start_report(diagnostics_report_, DiagnosticsReport::kFrameBytes, diagnostics, command, only);
}

void ConfigChannel::start_timing(const timing::SlotTimingStatistics& statistics) {
    start_report(timing_report_, kTimingFrameBytes, statistics);
}

template <class Report, class... Arguments>
void ConfigChannel::start_report(std::optional<Report>& report, int frame_bytes,
                                 const Arguments&... arguments) {
    report_payload_bytes_ = payload_bytes_to(claim_.holder());
    report_frame_bytes_ = frame_bytes;
    if (!report.emplace(arguments...).fits(report_payload_bytes_)) {
        link_drops_++;
        report.reset();
        refuse_too_wide();
        return;
    }
    continue_report(report);
}

// INFO: fc 25sep26 a frame held for the link still counts as sent, so the next one follows it
template <class Report>
void ConfigChannel::continue_report(std::optional<Report>& report) {
    uint8_t frame[kHeldFrameBytes];
    while (report && held_bytes_ == 0) {
        const int bytes = report->next_frame(report_payload_bytes_, frame, report_frame_bytes_);
        if (bytes <= 0) {
            link_drops_++;
            report.reset();
            return;
        }
        const Status sent = reply(frame, bytes);
        if (report->exhausted() || (sent != Status::Ok && sent != Status::WouldBlock))
            report.reset();
    }
}

void ConfigChannel::resume(uint32_t now_ms) {
    now_ms_ = now_ms;
    if (held_bytes_ > 0) {
        const Status sent =
            link_.send_to(held_for_, events::Endpoint::Config,
                          ConstByteSpan(held_frame_, static_cast<size_t>(held_bytes_)));
        if (sent == Status::WouldBlock && now_ms - held_since_ms_ < ports::kReplyWaitCeilingMs)
            return;
        held_bytes_ = 0;
        if (!is_ok(sent)) {
            link_drops_++;
            drop_replies();
            return;
        }
    }
    continue_report(diagnostics_report_);
    continue_report(timing_report_);
}

}  // namespace skyblip::comms
