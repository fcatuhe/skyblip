#include "core/comms/config.h"

#include <cstring>

#include "core/comms/timing_report.h"
#include "core/events/link.h"
#include "core/util/cbor.h"
#include "core/util/format.h"
#include "core/util/span.h"

namespace skyblip::comms {

namespace {
constexpr int kAcknowledgementFrameBytes = 16;

link::Reason settings_refusal(Status status) {
    return status == Status::OutOfRange ? link::Reason::OutOfRange : link::Reason::Invalid;
}
}  // namespace

void ConfigService::tick(uint32_t now_ms) {
    now_ms_ = now_ms;
    // INFO: fc 04aug26 The one frame nobody asked for is the one nobody will ask
    // for again, so a controller that was momentarily out of buffers gets another
    // pass rather than costing a pilot a stale gauge. Only a transient refusal
    // arms this: a frame too big for the payload would retry forever.
    if (status_push_due_ && link_up()) push_status();
    if (upload_window_open_ && now_ms - upload_window_opened_ms_ >= kUploadWindowMs) {
        upload_window_open_ = false;
    }
    if (upload_window_open_ && dfu_ != nullptr && dfu_->upload_finished()) install_received_image();
    if (identity_ != nullptr) identity_->tick(now_ms);
    if (pending_ != Pending::None && now_ms - pending_since_ms_ >= kPromptForgetMs) {
        pending_ = Pending::None;
        pending_set_bytes_ = 0;
        acknowledge(false, link::Reason::Expired);
    }
}

void ConfigService::set_flight_state(flight::FlightState state) {
    if (state == flight::FlightState::Airborne) {
        upload_window_open_ = false;
        // A prompt that survived the take-off roll would be an authorisation a
        // pilot could confirm with an elbow in the air.
        // INFO: fc 18oct26 activation is the exception: a unit with no serial transmits nothing
        if (pending_ != Pending::None && !register_exempt()) {
            pending_ = Pending::None;
            pending_set_bytes_ = 0;
            acknowledge(false, link::Reason::InFlight);
        }
    }
    const bool changed = state != flight_state_;
    flight_state_ = state;
    // INFO: fc 03oct26 a page holding its buttons for the ground learns it landed without polling
    if (changed && link_up()) push_status();
}

bool ConfigService::connected(uint16_t session_id) const {
    for (int i = 0; i < connected_count_; i++)
        if (connected_sessions_[i] == session_id) return true;
    return false;
}

void ConfigService::note_connected(uint16_t session_id) {
    if (connected(session_id) || connected_count_ == static_cast<int>(LinkSessions::kMaxSessions))
        return;
    connected_sessions_[connected_count_++] = session_id;
}

void ConfigService::note_disconnected(uint16_t session_id) {
    for (int i = 0; i < connected_count_; i++) {
        if (connected_sessions_[i] != session_id) continue;
        connected_sessions_[i] = connected_sessions_[--connected_count_];
        return;
    }
}

// INFO: fc 18sep26 A second Up on one session is the MTU exchange landing late, never a second app.
void ConfigService::on_link_up(const events::LinkUp& event) {
    note_connected(event.session_id);
    if (image_state_ != dfu::ImageState::Confirmed ||
        settings_fallback_ != settings::Fallback::None)
        send_update(event.session_id);
}

// INFO: fc 18sep26 Only the holder's disconnect cancels: another app leaving must not answer a
// prompt.
void ConfigService::on_link_down(const events::LinkDown& event) {
    note_disconnected(event.session_id);
    if (identity_ != nullptr) identity_->forget(event.session_id);
    if (!claim_.holds(event.session_id)) return;
    claim_.release();
    pending_ = Pending::None;
    pending_set_bytes_ = 0;
    upload_window_open_ = false;
    status_push_due_ = false;
    drop_replies();
}

namespace {
uint8_t battery_step(uint8_t percent) {
    return static_cast<uint8_t>(percent / kBatteryPushStepPercent);
}
}

void ConfigService::set_battery_state(const power::BatteryState& battery, power::PowerLevel level) {
    const bool charging_changed = battery.charging != diagnostics_.battery.charging;
    const bool level_changed = level != diagnostics_.power_level;
    const bool step_changed =
        battery_step(battery.percent) != battery_step(diagnostics_.battery.percent);

    diagnostics_.battery = battery;
    diagnostics_.power_level = level;

    if (link_up() && (charging_changed || level_changed || step_changed)) push_status();
}

int ConfigService::payload_bytes() const { return static_cast<int>(link_.payload_bytes()); }

int ConfigService::payload_bytes_to(uint16_t session_id) const {
    return static_cast<int>(link_.payload_bytes_to(session_id));
}

// INFO: fc 04aug26 The only door to the link, and it refuses out loud. A frame
// longer than the negotiated payload is not shortened by the controller, it
// fails, so it is counted here and never handed down. Everything that comes
// through this door is a request/response answer except the status push, so the
// recovery from a lost one is the phone's next command; the push has tick().
Status ConfigService::reply(const uint8_t* frame, int bytes) {
    return reply_to(claim_.holder(), frame, bytes);
}

Status ConfigService::broadcast(const uint8_t* frame, int bytes) {
    if (bytes <= 0 || bytes > payload_bytes()) {
        diagnostics_.link_drops++;
        return Status::OutOfRange;
    }
    const Status sent =
        link_.send(events::Endpoint::Config, ConstByteSpan(frame, static_cast<size_t>(bytes)));
    if (!is_ok(sent)) diagnostics_.link_drops++;
    return sent;
}

// INFO: fc 25sep26 one frame waits for the link, and a second one while it does is dropped as Full
Status ConfigService::reply_to(uint16_t session_id, const uint8_t* frame, int bytes) {
    if (bytes <= 0 || bytes > payload_bytes_to(session_id) || held_bytes_ > 0) {
        diagnostics_.link_drops++;
        return held_bytes_ > 0 ? Status::Full : Status::OutOfRange;
    }
    const Status sent = link_.send_to(session_id, events::Endpoint::Config,
                                      ConstByteSpan(frame, static_cast<size_t>(bytes)));
    if (sent == Status::WouldBlock)
        hold(session_id, frame, bytes);
    else if (!is_ok(sent))
        diagnostics_.link_drops++;
    return sent;
}

void ConfigService::hold(uint16_t session_id, const uint8_t* frame, int bytes) {
    if (bytes > kHeldFrameBytes) {
        diagnostics_.link_drops++;
        return;
    }
    std::memcpy(held_frame_, frame, static_cast<size_t>(bytes));
    held_bytes_ = bytes;
    held_for_session_id_ = session_id;
    held_since_ms_ = now_ms_;
}

void ConfigService::drop_replies() {
    held_bytes_ = 0;
    diagnostics_report_.reset();
    timing_report_.reset();
}

void ConfigService::refuse_unclaimed(uint16_t session_id) {
    uint8_t frame[kAcknowledgementFrameBytes];
    cbor::Writer writer(frame, sizeof(frame));
    writer.add_code(link::kCommandKey, link::Command::Acknowledgement);
    writer.add_boolean(link::acknowledgement_key::kAcknowledge, false);
    writer.add_code(link::acknowledgement_key::kReason, link::Reason::Claimed);
    writer.add_unsigned(link::acknowledgement_key::kBy, claim_.holder());
    (void)reply_to(session_id, frame, writer.finish());
}

void ConfigService::stage(Pending pending, link::Reason reason) {
    pending_ = pending;
    pending_since_ms_ = now_ms_;
    prompt_detail_[0] = 0;
    uint8_t frame[kAcknowledgementFrameBytes];
    cbor::Writer writer(frame, sizeof(frame));
    writer.add_code(link::kCommandKey, link::Command::Acknowledgement);
    writer.add_boolean(link::acknowledgement_key::kAcknowledge, false);
    writer.add_boolean(link::acknowledgement_key::kPending, true);
    writer.add_code(link::acknowledgement_key::kReason, reason);
    (void)reply(frame, writer.finish());
}

void ConfigService::acknowledge(bool accepted, std::optional<link::Reason> reason) {
    uint8_t frame[kAcknowledgementFrameBytes];
    cbor::Writer writer(frame, sizeof(frame));
    writer.add_code(link::kCommandKey, link::Command::Acknowledgement);
    writer.add_boolean(link::acknowledgement_key::kAcknowledge, accepted);
    if (reason) writer.add_code(link::acknowledgement_key::kReason, *reason);
    (void)reply(frame, writer.finish());
}

const char* ConfigService::prompt_detail() const {
    return prompt_detail_[0] != 0 ? prompt_detail_ : pending_detail(pending_);
}

// INFO: fc 03oct26 the version is what the press authorises, so the slot must hold that one
void ConfigService::request_firmware(const cbor::Reader& frame) {
    char text[dfu::kVersionTextBytes];
    ports::ImageVersion version;
    if (frame.read_text(link::dfu_key::kVersion, text, sizeof(text)) != Status::Ok ||
        !dfu::parse_version(text, version)) {
        acknowledge(false, link::Reason::NoVersion);
        return;
    }
    ports::ImageVersion running;
    if (dfu_ != nullptr && dfu_->running_version(running) &&
        dfu::version_refusal(running, version, dfu_->downgrade_allowed()) != nullptr) {
        acknowledge(false, link::Reason::Older);
        return;
    }
    approved_version_ = version;
    stage(Pending::Dfu, link::Reason::ConfirmDfu);
    int written = format_string(prompt_detail_, "INSTALL ");
    written +=
        dfu::format_version(version, prompt_detail_ + written, sizeof(prompt_detail_) - written);
    written += format_string(prompt_detail_ + written, " FROM THE PHONE");
    prompt_detail_[written] = 0;
}

// INFO: fc 03oct26 the one press authorised the whole update, so a finished upload installs unasked
void ConfigService::install_received_image() {
    upload_window_open_ = false;
    ports::ImageVersion staged;
    if (!dfu_->staged_version(staged)) {
        acknowledge(false, link::Reason::NothingStaged);
    } else if (staged != approved_version_) {
        acknowledge(false, link::Reason::NotApproved);
    } else if (!signed_by_trusted_key()) {
        acknowledge(false, link::Reason::WrongKey);
    } else if (!on_ground()) {
        acknowledge(false, link::Reason::InFlight);
    } else if (!swap_powered()) {
        acknowledge(false, link::Reason::LowPower);
    } else {
        install_requested_ = true;
        acknowledge(true, link::Reason::Install);
    }
}

// INFO: fc 03oct26 the bootloader verifies against this key, so a foreign image stops before the
// reboot
bool ConfigService::signed_by_trusted_key() const {
    ports::SigningKeyHash trusted;
    ports::SigningKeyHash signer;
    if (!dfu_->running_key(trusted)) return true;
    return dfu_->staged_key(signer) && signer == trusted;
}

void ConfigService::on_rx(const events::RxFrame& frame) {
    if (frame.endpoint != events::Endpoint::Config) return;
    if (!claim_.grant(frame.session_id)) {
        refuse_unclaimed(frame.session_id);
        return;
    }
    const cbor::Reader reader(ConstByteSpan(frame.data.data(), frame.bytes));
    const Result<uint32_t> code = reader.read_unsigned(link::kCommandKey);
    if (!code.ok()) {
        acknowledge(false, link::Reason::NoCommand);
        return;
    }
    const link::Command command = code.value() <= UINT8_MAX
                                      ? static_cast<link::Command>(code.value())
                                      : link::Command::Acknowledgement;
    switch (command) {
        case link::Command::Get: send_config(); return;
        case link::Command::Defaults: send_defaults(); return;
        case link::Command::Status: send_status(); return;
        case link::Command::Timing: send_timing(); return;
        case link::Command::Flash: send_flash(); return;
        case link::Command::Radio: send_radio(); return;
        case link::Command::Update: send_update(claim_.holder()); return;
        case link::Command::Diagnostics: send_diagnostics(); return;
        case link::Command::Set: stage_settings(frame); return;
        case link::Command::Dfu:
        case link::Command::Recovery:
        case link::Command::PowerOff:
        case link::Command::GnssColdStart: answer_prompt_request(command, reader); return;
        case link::Command::Challenge:
        case link::Command::Register:
        case link::Command::Unlock:
            if (answer_identity(command, reader)) return;
            break;
        case link::Command::Acknowledgement:
        case link::Command::Config:
        case link::Command::Registration:
        case link::Command::List:
        case link::Command::Read:
        case link::Command::Erase:
        case link::Command::Log:
        case link::Command::Session:
        case link::Command::Chunk:
        case link::Command::Ownship: break;
    }
    acknowledge(false, link::Reason::UnknownCommand);
}

// INFO: fc 04aug26 Sized at the smallest payload we support and refused rather
// than answered short: an app that receives a config object with fields missing
// renders defaults a pilot never set.
void ConfigService::send_config() {
    uint8_t frame[kSmallestSupportedPayloadBytes];
    cbor::Writer writer(frame, sizeof(frame));
    writer.add_code(link::kCommandKey, link::Command::Config);
    store_.write_fields(writer);
    const int bytes = writer.finish();
    if (bytes == 0) {
        diagnostics_.link_drops++;
        return;
    }
    (void)reply(frame, bytes);
}

void ConfigService::stage_settings(const events::RxFrame& frame) {
    if (!on_ground()) {
        acknowledge(false, link::Reason::InFlight);
        return;
    }
    // Refused at the door and not after the button press: a prompt a pilot
    // walks over and confirms, for a write that was never going to happen,
    // is worse than a refusal they can read on the phone in their hand.
    if (!settings_writable()) {
        acknowledge(false, link::Reason::LowPower);
        return;
    }
    char detail[sizeof(prompt_detail_)]{};
    const Status described =
        store_.describe_changes(cbor::Reader(ConstByteSpan(frame.data.data(), frame.bytes)), detail,
                                static_cast<int>(sizeof(detail)));
    if (described != Status::Ok) {
        acknowledge(false, settings_refusal(described));
        return;
    }
    pending_set_bytes_ = frame.bytes;
    std::memcpy(pending_set_frame_, frame.data.data(), frame.bytes);
    stage(Pending::Set, link::Reason::Confirm);
    std::memcpy(prompt_detail_, detail, sizeof(detail));
}

// INFO: fc 03oct26 the dfu image travels over SMP, not this channel (simulator/README.md)
void ConfigService::answer_prompt_request(link::Command command, const cbor::Reader& frame) {
    if (!on_ground()) {
        acknowledge(false, link::Reason::InFlight);
        return;
    }
    switch (command) {
        case link::Command::Dfu:
            if (!swap_powered()) {
                acknowledge(false, link::Reason::LowPower);
                return;
            }
            request_firmware(frame);
            return;
        case link::Command::Recovery:
            stage(Pending::Recovery, link::Reason::ConfirmRecovery);
            return;
        case link::Command::PowerOff:
            stage(Pending::PowerOff, link::Reason::ConfirmPowerOff);
            return;
        case link::Command::GnssColdStart:
            stage(Pending::GnssColdStart, link::Reason::ConfirmGnssColdStart);
            return;
        default: return;
    }
}

void ConfigService::request_log_erase() {
    if (!on_ground()) {
        acknowledge(false, link::Reason::InFlight);
        return;
    }
    stage(Pending::EraseLog, link::Reason::ConfirmEraseLog);
}

void ConfigService::confirm() {
    if (!on_ground() && !register_exempt()) {
        pending_ = Pending::None;
        pending_set_bytes_ = 0;
        acknowledge(false, link::Reason::InFlight);
        return;
    }
    if (pending_ == Pending::Register) {
        pending_ = Pending::None;
        send_registration();
        return;
    }
    if (pending_ == Pending::Set) {
        // Asked again: the confirmation window is kPromptForgetMs wide and a cell
        // can cross the warning inside it.
        if (!settings_writable()) {
            pending_ = Pending::None;
            pending_set_bytes_ = 0;
            acknowledge(false, link::Reason::LowPower);
            return;
        }
        const Status applied = store_.apply(cbor::Reader(
            ConstByteSpan(pending_set_frame_, static_cast<size_t>(pending_set_bytes_))));
        pending_ = Pending::None;
        pending_set_bytes_ = 0;
        if (applied == Status::Ok) {
            settings_dirty_ = true;
            acknowledge(true);
        } else {
            acknowledge(false, settings_refusal(applied));
        }
    } else if (pending_ == Pending::Dfu && !swap_powered()) {
        pending_ = Pending::None;
        upload_window_open_ = false;
        acknowledge(false, link::Reason::LowPower);
    } else if (pending_ == Pending::Dfu) {
        pending_ = Pending::None;
        upload_window_open_ = true;
        upload_window_opened_ms_ = now_ms_;
        if (dfu_) dfu_->forget_upload();
        acknowledge(true, link::Reason::Dfu);
    } else if (pending_ == Pending::Recovery) {
        pending_ = Pending::None;
        upload_window_open_ = false;
        recovery_requested_ = true;
        acknowledge(true, link::Reason::Recovery);
    } else if (pending_ == Pending::PowerOff) {
        pending_ = Pending::None;
        upload_window_open_ = false;
        power_off_requested_ = true;
        acknowledge(true, link::Reason::PowerOff);
    } else if (pending_ == Pending::EraseLog) {
        pending_ = Pending::None;
        log_erase_requested_ = true;
        acknowledge(true, link::Reason::EraseLog);
    } else if (pending_ == Pending::GnssColdStart) {
        pending_ = Pending::None;
        gnss_cold_start_requested_ = true;
        acknowledge(true, link::Reason::GnssColdStart);
    }
}

void ConfigService::cancel() {
    pending_ = Pending::None;
    pending_set_bytes_ = 0;
    upload_window_open_ = false;
    acknowledge(false, link::Reason::Cancelled);
}

}
