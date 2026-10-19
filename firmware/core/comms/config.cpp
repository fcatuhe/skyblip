#include "core/comms/config.h"

#include <cstring>

#include "core/comms/timing_report.h"
#include "core/events/link.h"
#include "core/util/format.h"
#include "core/util/json.h"
#include "core/util/span.h"

namespace skyblip::comms {

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
        acknowledge(false, "expired");
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
            acknowledge(false, "in_flight");
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
    const bool level_changed = level != diagnostics_.level;
    const bool step_changed =
        battery_step(battery.percent) != battery_step(diagnostics_.battery.percent);

    diagnostics_.battery = battery;
    diagnostics_.level = level;

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
Status ConfigService::reply(const char* json, int bytes) {
    return reply_to(claim_.holder(), json, bytes);
}

Status ConfigService::broadcast(const char* json, int bytes) {
    if (bytes <= 0 || bytes > payload_bytes()) {
        diagnostics_.link_drops++;
        return Status::OutOfRange;
    }
    const Status sent = link_.send(
        events::Endpoint::Config,
        ConstByteSpan(reinterpret_cast<const uint8_t*>(json), static_cast<size_t>(bytes)));
    if (!is_ok(sent)) diagnostics_.link_drops++;
    return sent;
}

// INFO: fc 25sep26 one frame waits for the link, and a second one while it does is dropped as Full
Status ConfigService::reply_to(uint16_t session_id, const char* json, int bytes) {
    if (bytes <= 0 || bytes > payload_bytes_to(session_id) || held_bytes_ > 0) {
        diagnostics_.link_drops++;
        return held_bytes_ > 0 ? Status::Full : Status::OutOfRange;
    }
    const Status sent = link_.send_to(
        session_id, events::Endpoint::Config,
        ConstByteSpan(reinterpret_cast<const uint8_t*>(json), static_cast<size_t>(bytes)));
    if (sent == Status::WouldBlock)
        hold(session_id, json, bytes);
    else if (!is_ok(sent))
        diagnostics_.link_drops++;
    return sent;
}

void ConfigService::hold(uint16_t session_id, const char* json, int bytes) {
    if (bytes > kHeldFrameBytes) {
        diagnostics_.link_drops++;
        return;
    }
    std::memcpy(held_frame_, json, static_cast<size_t>(bytes));
    held_bytes_ = bytes;
    held_for_session_id_ = session_id;
    held_since_ms_ = now_ms_;
}

void ConfigService::drop_replies() {
    held_bytes_ = 0;
    diagnostics_report_.reset();
    timing_report_.reset();
}

Status ConfigService::reply(const char* json) {
    return reply(json, static_cast<int>(std::strlen(json)));
}

void ConfigService::refuse_unclaimed(uint16_t session_id) {
    char buffer[96];
    json::Writer writer(buffer, sizeof(buffer));
    writer.add_boolean("ack", false);
    writer.add_string("reason", "claimed");
    writer.add_integer("by", static_cast<long>(claim_.holder()));
    const int bytes = writer.finish();
    (void)reply_to(session_id, buffer, bytes);
}

void ConfigService::stage(Pending pending, const char* reason) {
    pending_ = pending;
    pending_since_ms_ = now_ms_;
    prompt_detail_[0] = 0;
    char buffer[64];
    json::Writer writer(buffer, sizeof(buffer));
    writer.add_boolean("ack", false);
    writer.add_boolean("pending", true);
    writer.add_string("reason", reason);
    writer.finish();
    (void)reply(buffer);
}

void ConfigService::acknowledge(bool accepted, const char* reason) {
    char buffer[96];
    json::Writer writer(buffer, sizeof(buffer));
    writer.add_boolean("ack", accepted);
    if (reason) writer.add_string("reason", reason);
    writer.finish();
    (void)reply(buffer);
}

const char* ConfigService::prompt_detail() const {
    return prompt_detail_[0] != 0 ? prompt_detail_ : pending_detail(pending_);
}

// INFO: fc 03oct26 the version is what the press authorises, so the slot must hold that one
void ConfigService::request_firmware(const json::Reader& reader) {
    char text[dfu::kVersionTextBytes];
    ports::ImageVersion version;
    if (!reader.get_string("version", text, sizeof(text)) || !dfu::parse_version(text, version)) {
        acknowledge(false, "no_version");
        return;
    }
    ports::ImageVersion running;
    if (dfu_ != nullptr && dfu_->running_version(running) &&
        dfu::version_refusal(running, version, dfu_->downgrade_allowed()) != nullptr) {
        acknowledge(false, "older");
        return;
    }
    approved_version_ = version;
    stage(Pending::Dfu, "confirm_dfu");
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
        acknowledge(false, "nothing_staged");
    } else if (staged != approved_version_) {
        acknowledge(false, "not_approved");
    } else if (!signed_by_trusted_key()) {
        acknowledge(false, "wrong_key");
    } else if (!on_ground()) {
        acknowledge(false, "in_flight");
    } else if (!swap_powered()) {
        acknowledge(false, "low_power");
    } else {
        install_requested_ = true;
        acknowledge(true, "install");
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

const char* ConfigService::flight_name(flight::FlightState state) {
    switch (state) {
        case flight::FlightState::Ground: return "ground";
        case flight::FlightState::Airborne: return "airborne";
        case flight::FlightState::Unknown: break;
    }
    return "unknown";
}

void ConfigService::on_rx(const events::RxFrame& frame) {
    if (frame.endpoint != events::Endpoint::Config) return;
    if (!claim_.grant(frame.session_id)) {
        refuse_unclaimed(frame.session_id);
        return;
    }
    const char* data = reinterpret_cast<const char*>(frame.data.data());
    int bytes = frame.bytes;
    json::Reader reader(data, bytes);
    char command[16] = {0};
    if (!reader.get_string("cmd", command, sizeof(command))) {
        acknowledge(false, "no_cmd");
        return;
    }

    if (std::strcmp(command, "get") == 0) {
        // INFO: fc 04aug26 One flat object, which is also what
        // schemas/config.v1.schema.json describes: the settings used to travel
        // nested as an escaped string, and the backslashes alone were 53 bytes
        // on a 158-byte body. Sized by its buffer at the smallest payload we
        // support, and refused rather than answered short - an app that receives
        // a config object with fields missing renders defaults a pilot never set.
        char buffer[kSmallestSupportedPayloadBytes + 1];
        json::Writer writer(buffer, sizeof(buffer));
        writer.add_string("cmd", "config");
        store_.write_fields(writer);
        const int reply_bytes = writer.finish();
        if (writer.overflowed()) {
            diagnostics_.link_drops++;
            return;
        }
        (void)reply(buffer, reply_bytes);
        return;
    }

    if (std::strcmp(command, "defaults") == 0) {
        send_defaults();
        return;
    }

    if (std::strcmp(command, "status") == 0) {
        send_status();
        return;
    }

    if (std::strcmp(command, "timing") == 0) {
        send_timing();
        return;
    }

    if (std::strcmp(command, "flash") == 0) {
        send_flash();
        return;
    }

    if (std::strcmp(command, "radio") == 0) {
        send_radio();
        return;
    }

    if (std::strcmp(command, "update") == 0) {
        send_update(claim_.holder());
        return;
    }

    if (std::strcmp(command, "diag") == 0) {
        send_diagnostics();
        return;
    }

    if (answer_identity(command, reader)) return;

    if (std::strcmp(command, "set") == 0) {
        if (!on_ground()) {
            acknowledge(false, "in_flight");
            return;
        }
        // Refused at the door and not after the button press: a prompt a pilot
        // walks over and confirms, for a write that was never going to happen,
        // is worse than a refusal they can read on the phone in their hand.
        if (!settings_writable()) {
            acknowledge(false, "low_power");
            return;
        }
        pending_set_bytes_ = bytes < static_cast<int>(sizeof(pending_set_json_)) - 1
                                 ? bytes
                                 : static_cast<int>(sizeof(pending_set_json_)) - 1;
        std::memcpy(pending_set_json_, data, static_cast<size_t>(pending_set_bytes_));
        pending_set_json_[pending_set_bytes_] = 0;
        stage(Pending::Set, "confirm");
        (void)store_.describe_changes(pending_set_json_, pending_set_bytes_, prompt_detail_,
                                      static_cast<int>(sizeof(prompt_detail_)));
        return;
    }

    // INFO: fc 03oct26 the dfu image travels over SMP, not this channel (simulator/README.md)
    Pending requested = Pending::None;
    const char* reason = nullptr;
    if (std::strcmp(command, "dfu") == 0) {
        requested = Pending::Dfu;
        reason = "confirm_dfu";
    } else if (std::strcmp(command, "recovery") == 0) {
        requested = Pending::Recovery;
        reason = "confirm_recovery";
    } else if (std::strcmp(command, "power_off") == 0) {
        requested = Pending::PowerOff;
        reason = "confirm_power_off";
    } else if (std::strcmp(command, "gnss_cold") == 0) {
        requested = Pending::GnssColdStart;
        reason = "confirm_gnss_cold";
    }

    if (requested != Pending::None) {
        if (!on_ground()) {
            acknowledge(false, "in_flight");
            return;
        }
        if (requested == Pending::Dfu && !swap_powered()) {
            acknowledge(false, "low_power");
            return;
        }
        if (requested == Pending::Dfu)
            request_firmware(reader);
        else
            stage(requested, reason);
        return;
    }

    acknowledge(false, "unknown_cmd");
}

void ConfigService::request_log_erase() {
    if (!on_ground()) {
        acknowledge(false, "in_flight");
        return;
    }
    stage(Pending::EraseLog, "confirm_erase_log");
}

void ConfigService::confirm() {
    if (!on_ground() && !register_exempt()) {
        pending_ = Pending::None;
        pending_set_bytes_ = 0;
        acknowledge(false, "in_flight");
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
            acknowledge(false, "low_power");
            return;
        }
        Status applied = store_.apply(pending_set_json_, pending_set_bytes_);
        pending_ = Pending::None;
        pending_set_bytes_ = 0;
        if (applied == Status::Ok) {
            settings_dirty_ = true;
            acknowledge(true, nullptr);
        } else {
            acknowledge(false, to_string(applied));
        }
    } else if (pending_ == Pending::Dfu && !swap_powered()) {
        pending_ = Pending::None;
        upload_window_open_ = false;
        acknowledge(false, "low_power");
    } else if (pending_ == Pending::Dfu) {
        pending_ = Pending::None;
        upload_window_open_ = true;
        upload_window_opened_ms_ = now_ms_;
        if (dfu_) dfu_->forget_upload();
        acknowledge(true, "dfu");
    } else if (pending_ == Pending::Recovery) {
        pending_ = Pending::None;
        upload_window_open_ = false;
        recovery_requested_ = true;
        acknowledge(true, "recovery");
    } else if (pending_ == Pending::PowerOff) {
        pending_ = Pending::None;
        upload_window_open_ = false;
        power_off_requested_ = true;
        acknowledge(true, "power_off");
    } else if (pending_ == Pending::EraseLog) {
        pending_ = Pending::None;
        log_erase_requested_ = true;
        acknowledge(true, "erase_log");
    } else if (pending_ == Pending::GnssColdStart) {
        pending_ = Pending::None;
        gnss_cold_start_requested_ = true;
        acknowledge(true, "gnss_cold");
    }
}

void ConfigService::cancel() {
    pending_ = Pending::None;
    pending_set_bytes_ = 0;
    upload_window_open_ = false;
    acknowledge(false, "cancelled");
}

}
