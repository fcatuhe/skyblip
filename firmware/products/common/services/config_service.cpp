#include "products/common/services/config_service.h"

#include <cstring>

#include "core/events/link.h"
#include "core/util/cbor.h"
#include "core/util/span.h"

namespace skyblip::common {

namespace {
// INFO: fc 14nov26 Conflict is F.2.3's address check | 17nov26 Unsupported, a paired Bravo's alarm
link::Reason settings_refusal(Status status) {
    switch (status) {
        case Status::OutOfRange: return link::Reason::OutOfRange;
        case Status::Conflict: return link::Reason::NotOwnTransponder;
        case Status::Unsupported: return link::Reason::SetOnGo;
        default: return link::Reason::Invalid;
    }
}

bool stages_prompt(link::Command command) {
    switch (command) {
        case link::Command::Set:
        case link::Command::Dfu:
        case link::Command::Recovery:
        case link::Command::PowerOff:
        case link::Command::GnssColdStart:
        case link::Command::Register:
        case link::Command::Capture:
        case link::Command::Pair: return true;
        default: return false;
    }
}
}  // namespace

void ConfigService::tick(uint32_t now_ms) {
    channel_.tick(now_ms);
    // INFO: fc 04aug26 nobody asks for a push again, so a transient refusal retries it
    if (status_push_due_ && link_up()) push_status();
    install_held_ = upload_window_open_ && dfu_ != nullptr && dfu_->upload_finished();
    if (upload_window_open_ && !install_held_ &&
        now_ms - upload_window_opened_ms_ >= kUploadWindowMs)
        upload_window_open_ = false;
    if (install_held_ && !in_flight() && pending_ == Pending::None) install_received_image();
    if (identity_ != nullptr) identity_->tick(now_ms);
    if (pending_ != Pending::None && now_ms - pending_since_ms_ >= kPromptCeilingMs) {
        pending_ = Pending::None;
        pending_set_bytes_ = 0;
        acknowledge(false, link::Reason::Expired);
    }
}

void ConfigService::set_flight_state(flight::FlightState state) {
    const bool changed = state != flight_state_;
    flight_state_ = state;
    // INFO: fc 20nov26 a prompt that survived the take-off roll could be confirmed by an elbow
    if (pending_ != Pending::None && !prompt_allowed()) {
        pending_ = Pending::None;
        pending_set_bytes_ = 0;
        acknowledge(false, link::Reason::InFlight);
    }
    // INFO: fc 03oct26 a page holding its buttons in flight learns it landed without polling
    if (changed && link_up()) push_status();
}

// INFO: fc 18sep26 A second Up on one session is the MTU exchange landing late, never a second app.
void ConfigService::on_link_up(const events::LinkUp& event) {
    if (image_state_ != dfu::ImageState::Confirmed ||
        settings_fallback_ != settings::Fallback::None)
        push_update(events::Peer{event.session_id, event.connection});
}

// INFO: fc 18sep26 only the holder's disconnect cancels, so another app leaving answers no prompt
void ConfigService::on_link_down(const events::LinkDown& event) {
    diagnostics_.link_drop_reason = event.reason;
    forget_session(event.session_id);
    if (channel_.disconnect(event.session_id)) forget_holder();
}

// INFO: fc 26nov26 a full link_events queue drops a Down, so a gone holder's claim ends here
void ConfigService::release_departed_holder() {
    const uint16_t holder = channel_.claim().holder();
    if (!channel_.release_departed_holder()) return;
    forget_session(holder);
    forget_holder();
}

void ConfigService::forget_session(uint16_t session_id) {
    if (identity_ != nullptr) identity_->forget(session_id);
    browsers_.forget(session_id);
    if (pairing_peer_ && pairing_peer_->session_id == session_id) pairing_peer_.reset();
}

void ConfigService::forget_holder() {
    refused_on_device_ = false;
    pending_ = Pending::None;
    pending_set_bytes_ = 0;
    upload_window_open_ = false;
    status_push_due_ = false;
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

void ConfigService::set_bravo_state(comms::BravoState state) {
    const bool changed = state != bravo_;
    bravo_ = state;
    if (changed && link_up()) push_status();
}

void ConfigService::set_no_tx(timing::NoTx reason) {
    const bool changed = reason != no_tx_;
    no_tx_ = reason;
    if (changed && link_up()) push_status();
}

void ConfigService::set_own_transponder(const std::optional<comms::OwnTransponderHeard>& heard) {
    const bool changed = heard != own_transponder_;
    own_transponder_ = heard;
    if (changed && link_up()) push_status();
}

void ConfigService::stage(Pending pending, link::Reason reason) {
    pending_ = pending;
    pending_since_ms_ = channel_.now_ms();
    prompts_staged_++;
    prompt_detail_[0] = 0;
    channel_.acknowledge_pending(reason);
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
    if (trusted_browser()) {
        open_upload_window();
        acknowledge(true, link::Reason::Dfu);
        return;
    }
    stage(Pending::Dfu, link::Reason::ConfirmDfu);
    (void)format_dfu_detail(prompt_detail_, sizeof(prompt_detail_), version);
}

// INFO: fc 03oct26 the one press authorised the whole update, so a finished upload installs unasked
void ConfigService::install_received_image() {
    upload_window_open_ = false;
    install_held_ = false;
    ports::ImageVersion staged;
    if (!dfu_->staged_version(staged)) {
        refuse_landing(link::Reason::NothingStaged);
    } else if (staged != approved_version_) {
        refuse_landing(link::Reason::NotApproved);
    } else if (!signed_by_trusted_key()) {
        refuse_landing(link::Reason::WrongKey);
    } else if (!swap_powered()) {
        acknowledge(false, link::Reason::LowPower);
    } else {
        confirmed_ = Action::Install;
        acknowledge(true, link::Reason::Install);
    }
}

void ConfigService::refuse_landing(link::Reason reason) {
    dfu::Landing landing{dfu_->check_staged()};
    ports::SigningKeyHash key;
    if (dfu_->staged_key(key)) landing.staged_key = key;
    landing_ = landing;
    refuse(reason);
}

// INFO: fc 03oct26 a key MCUboot refuses stops here, before the reboot | 15nov26 read off MCUboot
bool ConfigService::signed_by_trusted_key() const {
    ports::SigningKeyHash signer;
    ports::BootloaderKeys bootloader;
    if (dfu_->bootloader_keys(bootloader))
        return dfu_->staged_key(signer) && bootloader.trusts(signer);
    ports::SigningKeyHash trusted;
    if (!dfu_->running_key(trusted)) return true;
    return dfu_->staged_key(signer) && signer == trusted;
}

void ConfigService::on_rx(const events::RxFrame& frame) {
    if (frame.endpoint != events::Endpoint::Config) return;
    if (!channel_.admit(events::peer_of(frame))) return;
    const cbor::Reader reader(ConstByteSpan(frame.data.data(), frame.bytes));
    const Result<uint32_t> code = reader.read_unsigned(link::kCommandKey);
    if (!code.ok()) {
        acknowledge(false, link::Reason::NoCommand);
        return;
    }
    const link::Command command = code.value() <= UINT8_MAX
                                      ? static_cast<link::Command>(code.value())
                                      : link::Command::Acknowledgement;
    if (!answers(command)) {
        acknowledge(false, link::Reason::UnknownCommand);
        return;
    }
    if (!reader.keys_within(link::host_keys(command))) {
        acknowledge(false, link::Reason::Invalid);
        return;
    }
    if (stages_prompt(command) && prompt_refused()) return;
    switch (command) {
        case link::Command::Get: send_config(); return;
        case link::Command::Defaults: send_defaults(); return;
        case link::Command::Status: send_status(); return;
        case link::Command::Timing: send_timing(); return;
        case link::Command::Flash: send_flash(); return;
        case link::Command::Radio: send_radio(); return;
        case link::Command::Update: send_update(); return;
        case link::Command::Diagnostics: send_diagnostics(); return;
        case link::Command::Set: stage_settings(frame); return;
        case link::Command::Dfu:
        case link::Command::Recovery:
        case link::Command::PowerOff:
        case link::Command::GnssColdStart: answer_prompt_request(command, reader); return;
        case link::Command::Challenge: challenge(); return;
        case link::Command::Register: register_unit(reader); return;
        case link::Command::Unlock: unlock(reader); return;
        case link::Command::Attest: attest(reader); return;
        case link::Command::Capture: request_capture(reader); return;
        case link::Command::Pair: request_pairing(reader); return;
        case link::Command::BrowserChallenge: browser_challenge(); return;
        case link::Command::TrustBrowser: trust_browser(reader); return;
        case link::Command::Browsers: send_browser(reader); return;
        case link::Command::ForgetBrowser: forget_browser(reader); return;
        case link::Command::InstallReport: send_install_report(); return;
        case link::Command::Protocols: send_protocols(); return;
        case link::Command::Identity: send_identity(); return;
        default: return;
    }
}

bool ConfigService::answers(link::Command command) const {
    switch (command) {
        case link::Command::Get:
        case link::Command::Defaults:
        case link::Command::Status:
        case link::Command::Timing:
        case link::Command::Flash:
        case link::Command::Radio:
        case link::Command::Update:
        case link::Command::InstallReport:
        case link::Command::Diagnostics:
        case link::Command::Set:
        case link::Command::Dfu:
        case link::Command::Recovery:
        case link::Command::PowerOff:
        case link::Command::GnssColdStart: return true;
        case link::Command::Challenge:
        case link::Command::Register:
        case link::Command::Unlock:
        case link::Command::Attest:
        case link::Command::Identity:
        case link::Command::BrowserChallenge:
        case link::Command::TrustBrowser:
        case link::Command::Browsers:
        case link::Command::ForgetBrowser: return identity_ != nullptr;
        case link::Command::Capture: return serves_captures_;
        case link::Command::Protocols: return !radio_protocols_.empty();
        case link::Command::Pair: return pairing_side_ != PairingSide::None;
        case link::Command::Acknowledgement:
        case link::Command::Config:
        case link::Command::Registration:
        case link::Command::Attestation:
        case link::Command::List:
        case link::Command::Read:
        case link::Command::Erase:
        case link::Command::Log:
        case link::Command::Session:
        case link::Command::Chunk:
        case link::Command::Ownship:
        case link::Command::Observation:
        case link::Command::Health:
        case link::Command::PeerClaim:
        case link::Command::Browser: return false;
    }
    return false;
}

// INFO: fc 04aug26 refused, never answered short: a config reply missing a field renders defaults
void ConfigService::send_config() {
    uint8_t frame[comms::kSmallestSupportedPayloadBytes];
    cbor::Writer writer(frame, sizeof(frame));
    writer.add_code(link::kCommandKey, link::Command::Config);
    store_.write_fields(writer);
    if (paired_tag_) writer.add_unsigned(link::config_key::kPairedTag, *paired_tag_);
    const int bytes = writer.finish();
    if (bytes == 0) {
        diagnostics_.link_drops++;
        return;
    }
    (void)reply(frame, bytes);
}

void ConfigService::stage_settings(const events::RxFrame& frame) {
    if (in_flight() && !trusted_browser()) {
        acknowledge(false, link::Reason::InFlight);
        return;
    }
    // INFO: fc 20oct26 refused at the door: a pilot must never confirm a write that cannot happen
    if (!settings_writable()) {
        acknowledge(false, link::Reason::LowPower);
        return;
    }
    const cbor::Reader reader(ConstByteSpan(frame.data.data(), frame.bytes));
    char detail[sizeof(prompt_detail_)]{};
    const Status described =
        store_.describe_changes(reader, detail, static_cast<int>(sizeof(detail)));
    if (described != Status::Ok) {
        acknowledge(false, settings_refusal(described));
        return;
    }
    if (trusted_browser()) {
        apply_settings(reader);
        return;
    }
    pending_set_bytes_ = frame.bytes;
    std::memcpy(pending_set_frame_, frame.data.data(), frame.bytes);
    stage(Pending::Set, link::Reason::Confirm);
    std::memcpy(prompt_detail_, detail, sizeof(detail));
}

void ConfigService::apply_settings(const cbor::Reader& frame) {
    const Status applied = store_.apply(frame);
    if (applied != Status::Ok) {
        acknowledge(false, settings_refusal(applied));
        return;
    }
    remember_browser();
    settings_dirty_ = true;
    acknowledge(true);
}

void ConfigService::open_upload_window() {
    upload_window_open_ = true;
    upload_window_opened_ms_ = channel_.now_ms();
    landing_.reset();
    if (dfu_) dfu_->forget_upload();
}

}  // namespace skyblip::common
