#ifndef SKYBLIP_PRODUCTS_COMMON_SERVICES_CONFIG_SERVICE_H
#define SKYBLIP_PRODUCTS_COMMON_SERVICES_CONFIG_SERVICE_H

#include <optional>

#include "core/comms/browser_link.h"
#include "core/comms/config.h"
#include "core/comms/diagnostics.h"
#include "core/comms/identity_link.h"
#include "core/comms/link_claim.h"
#include "core/comms/link_registry.h"
#include "core/comms/peer.h"
#include "core/comms/radio_protocols.h"
#include "core/dfu/install_report.h"
#include "core/dfu/update.h"
#include "core/events/link.h"
#include "core/flight/state.h"
#include "core/identity/variant.h"
#include "core/power/battery.h"
#include "core/power/cutoff.h"
#include "core/power/reset_reason.h"
#include "core/settings/blob.h"
#include "core/timing/durable_write.h"
#include "core/timing/slot_timing_statistics.h"
#include "core/timing/transmit_gate.h"
#include "core/util/cbor.h"
#include "core/util/span.h"
#include "ports/dfu.h"
#include "ports/link.h"
#include "products/common/settings_store.h"

namespace skyblip::common {

enum class Pending : uint8_t {
    None = 0,
    Set = 1,
    Dfu = 2,
    // INFO: fc 03oct26 3 was apply, retired: captures and the diagnostics schema keep its number
    Recovery = 4,
    PowerOff = 5,
    EraseLog = 6,
    GnssColdStart = 7,
    Register = 8,
    Capture = 9,
    Pair = 10
};

enum class Action : uint8_t { PowerOff, Install, Recovery, EraseLog, GnssColdStart, Capture };

struct Confirmed {
    Action action;
    comms::CaptureProfile capture{comms::CaptureProfile::Stop};
    events::Peer peer{0, 0};
};

// INFO: fc 16nov26 a Go asks an untrusted browser's pair on its glass, Bravo has none to ask on
enum class PairingSide : uint8_t { None, Go, Bravo };

// INFO: fc 20nov26 the glass and the phone read these same strings, so both name one authorisation
const char* pending_title(Pending pending);
const char* pending_detail(Pending pending);
int format_dfu_detail(char* out, size_t capacity, const ports::ImageVersion& version);
int format_capture_detail(char* out, comms::CaptureProfile profile, uint32_t evicts_flights);
int format_pair_detail(char* out, uint16_t tag);

// INFO: cf 02aug26 an unanswered prompt is an open authorisation a stranger can walk up to
constexpr uint32_t kPromptCeilingMs = 30u * 1000u;

// INFO: cf 02aug26 a push is earned by a step this wide, never by ADC noise, or it polls the link
constexpr uint8_t kBatteryPushStepPercent = 5;

constexpr int kPromptDetailBytes = 128;

class ConfigService {
   public:
    ConfigService(ports::Link& link, SettingsStore& store, ports::Dfu* dfu = nullptr,
                  const timing::SlotTimingStatistics* timing_statistics = nullptr)
        : channel_(link, diagnostics_.link_drops),
          store_(store),
          dfu_(dfu),
          timing_statistics_(timing_statistics) {}

    // INFO: fc 11nov26 only Airborne closes this door, held through a lost fix in flight/ground.h
    void set_flight_state(flight::FlightState state);
    flight::FlightState flight_state() const { return flight_state_; }

    void set_durable_writes(const timing::DurableWriteWindow* writes) { durable_writes_ = writes; }

    // INFO: fc 18oct26 challenge, register, unlock live in config_identity.cpp, else
    // unknown_command
    void set_identity(comms::IdentityLink* identity) { identity_ = identity; }
    // INFO: fc 18oct26 an activated unit's radio runs, so its product places the page write
    bool unlock_due() const { return identity_ != nullptr && identity_->unlock_due(); }
    // INFO: fc 18oct26 writes the held code if the power rule allows, else drops it as low_power
    void settle_unlock();

    comms::TrustedBrowsers& trusted_browsers() { return browsers_.trusted(); }
    bool browsers_dirty() const { return browsers_dirty_; }
    void note_browsers_changed() { browsers_dirty_ = true; }
    void clear_browsers_dirty() { browsers_dirty_ = false; }

    void tick(uint32_t now_ms);

    void resume_replies(uint32_t now_ms) { channel_.resume(now_ms); }
    // INFO: fc 20nov26 while true the next command waits on the bus: its answer has nowhere to go
    bool replying() const { return channel_.replying(); }

    // INFO: fc 11nov26 SMP is unauthenticated, so an image lands only in a window the pilot opened
    bool upload_allowed() const { return upload_window_open_ && !install_held_; }
    // INFO: fc 13nov26 in flight the glass keeps the radar while an upload runs on behind it
    bool receiving_firmware() const { return upload_allowed() && !in_flight(); }
    bool updating() const { return upload_window_open_; }

    void on_link_up(const events::LinkUp& event);
    void on_link_down(const events::LinkDown& event);
    void release_departed_holder();
    void on_rx(const events::RxFrame& frame);
    bool from_live_connection(const events::RxFrame& frame) const {
        return channel_.from_live_connection(frame);
    }

    bool link_up() const { return channel_.link_up(); }
    bool status_push_due() const { return status_push_due_; }
    const comms::LinkClaim& claim() const { return channel_.claim(); }
    bool claim_link(const events::Peer& peer) { return channel_.claim().grant(peer); }
    uint16_t claim_holder() const { return channel_.claim().holder(); }

    void set_battery_state(const power::BatteryState& battery, power::PowerLevel level);

    void set_die_temperature(int16_t die_dc, bool valid) {
        diagnostics_.die_dc = die_dc;
        diagnostics_.die_valid = valid;
    }

    comms::Diagnostics& diagnostics() { return diagnostics_; }
    const comms::Diagnostics& diagnostics() const { return diagnostics_; }

    void set_range_refused(uint32_t count) { diagnostics_.range_refused = count; }

    void set_supply_warned(bool warned) { supply_warned_ = warned; }

    // INFO: fc 20nov26 a refused set is told to the phone, never accepted and quietly not written
    bool settings_writable() const {
        return power::may_write(diagnostics_.power_level, supply_warned_,
                                power::DurableWrite::Settings);
    }

    // INFO: fc 07sep26 a swap is 15-25 s of erases on the cell, and probation ends next boot
    bool swap_powered() const { return settings_writable(); }

    void set_image_state(dfu::ImageState state, const dfu::UpdateRecord& record) {
        image_state_ = state;
        update_record_ = record;
    }
    dfu::ImageState image_state() const { return image_state_; }

    void set_install_report(const dfu::InstallReport* report) { install_report_ = report; }

    void set_settings_fallback(settings::Fallback fallback) { settings_fallback_ = fallback; }

    void set_hub_image(const dfu::HubImageReport& report) { hub_image_ = report; }

    void set_product(dfu::Product product) { product_ = product; }

    void set_variant(identity::Variant variant) { variant_ = variant; }

    void set_radio_protocols(Span<const comms::RadioProtocol> protocols) {
        radio_protocols_ = protocols;
    }

    // INFO: cf 02aug26 BLE pairing is off, so these two doors take only a gesture on the device
    void confirm();
    void cancel();

    Pending pending() const { return pending_; }
    // INFO: fc 11nov26 a new count is a new prompt, even of the same kind as the one it replaced
    uint32_t prompts_staged() const { return prompts_staged_; }
    // INFO: fc 10nov26 the screen asks with the latch of this pass, which config hears a pass later
    bool prompt_allowed_in(flight::FlightState state) const;
    const char* prompt_detail() const;
    // INFO: fc 15nov26 the press on this prompt also stores the browser's proven key
    bool prompt_trusts_browser() const {
        return (pending_ == Pending::Set || pending_ == Pending::Dfu ||
                pending_ == Pending::Register || pending_ == Pending::Pair) &&
               browsers_.enrolls(channel_.claim().peer());
    }
    uint32_t prompt_seconds_left(uint32_t now_ms) const;
    bool settings_dirty() const { return settings_dirty_; }
    void clear_dirty() { settings_dirty_ = false; }

    // INFO: fc 10nov26 one writer stores the blob, on this flag, so the panel and a phone agree
    void note_settings_changed() { settings_dirty_ = true; }

    void set_reset_reason(power::ResetReason reason) { diagnostics_.reset = reason; }
    power::ResetReason reset_reason() const { return diagnostics_.reset; }
    void set_went_dark_flat(bool flat) { went_dark_flat_ = flat; }
    // INFO: fc 12nov26 a paired Go's glass shows its Bravo lost, and a page learns it unpolled
    void set_bravo_state(comms::BravoState state);
    // INFO: fc 15nov26 a page proposes the transponder Bravo hears, as the Go's AIRCRAFT menu does
    void set_own_transponder(const std::optional<comms::OwnTransponderHeard>& heard);
    // INFO: fc 19nov26 the reason the glass shows beside NO TX, pushed when it changes
    void set_no_tx(timing::NoTx reason);

    // INFO: fc 20nov26 a product that never calls this answers capture unknown_command
    void serve_captures() { serves_captures_ = true; }
    void set_capture(bool available, bool armed) {
        capture_available_ = available;
        capture_armed_ = armed;
    }

    // INFO: fc 16nov26 a Go and a Bravo answer pair, each told the tag of the other (README.md)
    void serve_pairing(PairingSide side) { pairing_side_ = side; }
    void set_paired_tag(std::optional<uint16_t> tag) { paired_tag_ = tag; }
    std::optional<uint16_t> take_pair_request();
    // INFO: fc 16nov26 the Go's verdict goes unasked to the browser that asked, while connected
    void pairing_ended(link::Reason verdict);

    // INFO: fc 19nov26 an erase destroys a pilot's evidence, so only a press on the unit allows it
    void request_log_erase();

    // INFO: fc 19nov26 the product takes, each pass, what a press confirmed or a finished upload
    std::optional<Confirmed> take_confirmed() {
        if (!confirmed_) return std::nullopt;
        const Confirmed taken{*confirmed_, staged_capture_, log_erase_peer_};
        confirmed_.reset();
        return taken;
    }

    // INFO: fc 04aug26 every frame the link did not take, since silence towards a phone is a fault
    uint32_t link_drops() const { return diagnostics_.link_drops; }

   private:
    Status reply(const uint8_t* frame, int bytes) { return channel_.reply(frame, bytes); }
    void acknowledge(bool accepted, std::optional<link::Reason> reason = std::nullopt) {
        channel_.acknowledge(accepted, reason);
    }
    void refuse(link::Reason reason) { acknowledge(false, reason); }
    void refuse_too_wide() { channel_.refuse_too_wide(); }
    bool prompt_refused();
    void forget_peer(const events::Peer& peer);
    void forget_holder();
    void stage(Pending pending, link::Reason reason);
    void send_config();
    void send_status();
    void send_defaults();
    void push_status();
    int format_status(uint8_t* out, int capacity);
    void send_timing();
    void send_flash();
    void send_radio();
    void send_update();
    void send_install_report();
    void send_protocols();
    void send_identity();
    bool carries(comms::RadioProtocol protocol) const;
    void push_update(const events::Peer& peer);
    int format_update(uint8_t* out, int capacity);
    void send_diagnostics();
    bool answers(link::Command command) const;
    void challenge();
    void register_unit(const cbor::Reader& frame);
    void send_registration();
    void attest(const cbor::Reader& frame);
    bool running_version_text(char* out, size_t capacity) const;
    bool identity_refused_in_flight();
    void unlock(const cbor::Reader& frame);
    void browser_challenge();
    void trust_browser(const cbor::Reader& frame);
    void send_browser(const cbor::Reader& frame);
    void forget_browser(const cbor::Reader& frame);
    void remember_browser();
    bool trusted_browser() const { return browsers_.trusts(channel_.claim().peer()); }
    void request_firmware(const cbor::Reader& frame);
    void stage_settings(const events::RxFrame& frame);
    void answer_prompt_request(link::Command command, const cbor::Reader& frame);
    void request_capture(const cbor::Reader& frame);
    void request_pairing(const cbor::Reader& frame);
    void start_pairing(uint16_t tag);
    void install_received_image();
    void refuse_landing(link::Reason reason);
    void open_upload_window();
    void apply_settings(const cbor::Reader& frame);
    bool signed_by_trusted_key() const;
    bool in_flight() const { return flight_state_ == flight::FlightState::Airborne; }
    bool activated() const { return identity_ != nullptr && identity_->activated(); }
    bool register_exempt() const { return pending_ == Pending::Register && !activated(); }
    bool prompt_allowed() const { return prompt_allowed_in(flight_state_); }
    cbor::Reader pending_set() const;

    static_assert(comms::ConfigChannel::kHeldFrameBytes >=
                      comms::IdentityLink::kRegistrationFrameBytes,
                  "a registration frame must be holdable");

    comms::Diagnostics diagnostics_{};
    comms::ConfigChannel channel_;
    SettingsStore& store_;
    ports::Dfu* dfu_;
    const timing::SlotTimingStatistics* timing_statistics_;
    const timing::DurableWriteWindow* durable_writes_{nullptr};
    comms::IdentityLink* identity_{nullptr};
    comms::BrowserLink browsers_{};
    bool browsers_dirty_{false};
    flight::FlightState flight_state_{flight::FlightState::Unknown};
    bool supply_warned_{false};
    bool went_dark_flat_{false};
    comms::BravoState bravo_{comms::BravoState::Unpaired};
    std::optional<comms::OwnTransponderHeard> own_transponder_{};
    timing::NoTx no_tx_{timing::NoTx::None};
    bool status_push_due_{false};
    std::optional<Action> confirmed_{};
    events::Peer log_erase_peer_{0, 0};
    bool serves_captures_{false};
    bool capture_available_{false};
    bool capture_armed_{false};
    comms::CaptureProfile staged_capture_{comms::CaptureProfile::Stop};
    PairingSide pairing_side_{PairingSide::None};
    std::optional<uint16_t> paired_tag_{};
    std::optional<uint16_t> pair_requested_{};
    uint16_t staged_pair_tag_{0};
    std::optional<events::Peer> pairing_peer_{};
    Pending pending_{Pending::None};
    bool refused_on_device_{false};
    bool settings_dirty_{false};
    bool upload_window_open_{false};
    // INFO: fc 13nov26 an image that lands in flight installs on the ground, since a swap goes dark
    bool install_held_{false};
    uint32_t upload_window_opened_ms_{0};
    uint32_t pending_since_ms_{0};
    uint32_t prompts_staged_{0};
    ports::ImageVersion approved_version_{};
    char prompt_detail_[kPromptDetailBytes]{};
    uint8_t pending_set_frame_[sizeof(events::RxFrame::data)]{};
    int pending_set_bytes_{0};
    dfu::ImageState image_state_{dfu::ImageState::Confirmed};
    dfu::UpdateRecord update_record_{};
    const dfu::InstallReport* install_report_{nullptr};
    std::optional<dfu::Landing> landing_{};
    settings::Fallback settings_fallback_{settings::Fallback::None};
    dfu::HubImageReport hub_image_{};
    dfu::Product product_{dfu::Product::Go};
    identity::Variant variant_{identity::Variant::Standard};
    Span<const comms::RadioProtocol> radio_protocols_{};

    // INFO: fc 20nov26 ~730 KB over BLE on a slow phone, short of a bench writable all afternoon
    static constexpr uint32_t kUploadWindowMs = 10u * 60u * 1000u;
};

}  // namespace skyblip::common

#endif
