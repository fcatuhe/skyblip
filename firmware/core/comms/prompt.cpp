#include "core/comms/config.h"
#include "core/dfu/update.h"
#include "core/util/format.h"

namespace skyblip::comms {

const char* pending_title(Pending pending) {
    switch (pending) {
        case Pending::Set: return "SETTINGS";
        case Pending::Dfu: return "FIRMWARE";
        case Pending::Recovery: return "RECOVERY";
        case Pending::PowerOff: return "POWER OFF";
        case Pending::EraseLog: return "ERASE LOGS";
        case Pending::GnssColdStart: return "ERASE GNSS DATA";
        case Pending::Register: return "ACTIVATE";
        case Pending::Capture: return "CAPTURE";
        case Pending::Pair: return "PAIR BRAVO";
        case Pending::None: break;
    }
    return "";
}

const char* pending_detail(Pending pending) {
    switch (pending) {
        case Pending::Set: return "CHANGE TO THE SETTINGS THAT THE PHONE SENT";
        case Pending::Dfu: return "INSTALL THE FIRMWARE THAT THE PHONE SENDS";
        case Pending::Recovery: return "START THE USB BOOTLOADER";
        case Pending::PowerOff: return "STOP THE DEVICE";
        case Pending::EraseLog: return "ERASE ALL FLIGHT DATA ON THE DEVICE";
        case Pending::GnssColdStart: return "ERASE THE SATELLITE DATA IN THE RECEIVER";
        case Pending::Register: return "SEND THE KEY OF THIS UNIT TO SKYBLIP.EU";
        case Pending::Capture: return "START OR STOP THE DIAGNOSTIC CAPTURE";
        case Pending::Pair: return "PAIR WITH THE BRAVO THAT THE PHONE NAMES";
        case Pending::None: break;
    }
    return "";
}

// INFO: fc 15nov26 three rows of sixteen at double height, so the profile and the price each fit
int format_capture_detail(char* out, CaptureProfile profile, uint32_t evicts_flights) {
    if (profile == CaptureProfile::Stop) return format_string(out, "STOP THE CAPTURE");
    int written = format_string(out, "START A ");
    written += format_string(out + written, capture_profile_word(profile));
    if (profile == CaptureProfile::Full) written += format_string(out + written, " CAPTURE");
    if (evicts_flights == 0) return written;
    written += format_string(out + written, ", ERASES ");
    written += format_unsigned(out + written, evicts_flights);
    return written + format_string(out + written, evicts_flights == 1 ? " FLIGHT" : " FLIGHTS");
}

int format_pair_detail(char* out, uint16_t tag) {
    int written = format_string(out, "PAIR WITH BRAVO ");
    written += format_hex(out + written, tag, 4);
    out[written] = 0;
    return written;
}

int format_dfu_detail(char* out, size_t capacity, const ports::ImageVersion& version) {
    int written = format_string(out, "INSTALL ");
    written += dfu::format_version(version, out + written, capacity - static_cast<size_t>(written));
    written += format_string(out + written, " FROM THE PHONE");
    out[written] = 0;
    return written;
}

bool ConfigService::prompt_allowed_in(flight::FlightState state) const {
    // INFO: fc 18oct26 activation is the exception: a unit with no serial transmits nothing
    return state != flight::FlightState::Airborne || register_exempt();
}

cbor::Reader ConfigService::pending_set() const {
    return cbor::Reader(ConstByteSpan(pending_set_frame_, static_cast<size_t>(pending_set_bytes_)));
}

// INFO: fc 10nov26 no command renews a prompt, and one press ends prompts for that link
bool ConfigService::prompt_refused() {
    if (pending_ != Pending::None) {
        acknowledge(false, link::Reason::PromptPending);
        return true;
    }
    if (refused_on_device_) {
        acknowledge(false, link::Reason::RefusedOnDevice);
        return true;
    }
    return false;
}

const char* ConfigService::prompt_detail() const {
    return prompt_detail_[0] != 0 ? prompt_detail_ : pending_detail(pending_);
}

uint32_t ConfigService::prompt_seconds_left(uint32_t now_ms) const {
    const uint32_t standing_ms = now_ms - pending_since_ms_;
    if (pending_ == Pending::None || standing_ms >= kPromptCeilingMs) return 0;
    return (kPromptCeilingMs - standing_ms + 999u) / 1000u;
}

// INFO: fc 03oct26 the dfu image travels over SMP, not this channel (simulator/README.md)
void ConfigService::answer_prompt_request(link::Command command, const cbor::Reader& frame) {
    if (in_flight()) {
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
    if (prompt_refused()) return;
    if (in_flight()) {
        acknowledge(false, link::Reason::InFlight);
        return;
    }
    stage(Pending::EraseLog, link::Reason::ConfirmEraseLog);
}

void ConfigService::confirm() {
    if (!prompt_allowed()) {
        pending_ = Pending::None;
        pending_set_bytes_ = 0;
        acknowledge(false, link::Reason::InFlight);
        return;
    }
    if (pending_ == Pending::Register) {
        pending_ = Pending::None;
        remember_browser();
        send_registration();
        return;
    }
    if (pending_ == Pending::Pair) {
        pending_ = Pending::None;
        remember_browser();
        start_pairing(staged_pair_tag_);
        return;
    }
    if (pending_ == Pending::Set) {
        // INFO: fc 20nov26 asked again, since a cell can cross the warning while a prompt stands
        if (!settings_writable()) {
            pending_ = Pending::None;
            pending_set_bytes_ = 0;
            acknowledge(false, link::Reason::LowPower);
            return;
        }
        const cbor::Reader staged = pending_set();
        pending_ = Pending::None;
        pending_set_bytes_ = 0;
        apply_settings(staged);
    } else if (pending_ == Pending::Dfu && !swap_powered()) {
        pending_ = Pending::None;
        upload_window_open_ = false;
        acknowledge(false, link::Reason::LowPower);
    } else if (pending_ == Pending::Dfu) {
        pending_ = Pending::None;
        remember_browser();
        open_upload_window();
        acknowledge(true, link::Reason::Dfu);
    } else if (pending_ == Pending::Recovery) {
        pending_ = Pending::None;
        upload_window_open_ = false;
        confirmed_ = Action::Recovery;
        acknowledge(true, link::Reason::Recovery);
    } else if (pending_ == Pending::PowerOff) {
        pending_ = Pending::None;
        upload_window_open_ = false;
        confirmed_ = Action::PowerOff;
        acknowledge(true, link::Reason::PowerOff);
    } else if (pending_ == Pending::EraseLog) {
        pending_ = Pending::None;
        confirmed_ = Action::EraseLog;
        log_erase_peer_ = claim_.peer();
        acknowledge(true, link::Reason::EraseLog);
    } else if (pending_ == Pending::GnssColdStart) {
        pending_ = Pending::None;
        confirmed_ = Action::GnssColdStart;
        acknowledge(true, link::Reason::GnssColdStart);
    } else if (pending_ == Pending::Capture) {
        pending_ = Pending::None;
        confirmed_ = Action::Capture;
        acknowledge(true, link::Reason::Capture);
    }
}

void ConfigService::cancel() {
    if (pending_ != Pending::None) refused_on_device_ = true;
    pending_ = Pending::None;
    pending_set_bytes_ = 0;
    upload_window_open_ = false;
    acknowledge(false, link::Reason::Cancelled);
}

}  // namespace skyblip::comms
