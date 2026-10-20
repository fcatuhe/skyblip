#ifndef SKYBLIP_CORE_COMMS_CONFIG_H
#define SKYBLIP_CORE_COMMS_CONFIG_H

#include <optional>

#include "core/comms/config_store.h"
#include "core/comms/diagnostics.h"
#include "core/comms/identity_link.h"
#include "core/comms/link_claim.h"
#include "core/comms/link_registry.h"
#include "core/comms/link_sessions.h"
#include "core/comms/timing_report.h"
#include "core/dfu/update.h"
#include "core/events/link.h"
#include "core/flight/state.h"
#include "core/power/battery.h"
#include "core/power/cutoff.h"
#include "core/power/reset_reason.h"
#include "core/settings/blob.h"
#include "core/timing/durable_write.h"
#include "core/timing/slot_timing_statistics.h"
#include "core/util/cbor.h"
#include "ports/dfu.h"
#include "ports/link.h"

namespace skyblip::comms {

enum class Pending : uint8_t {
    None = 0,
    Set = 1,
    Dfu = 2,
    // INFO: fc 03oct26 3 was apply, retired: captures and the diagnostics schema keep its number
    Recovery = 4,
    PowerOff = 5,
    EraseLog = 6,
    GnssColdStart = 7,
    Register = 8
};

// What a prompt says it is, and what confirming it will do. The panel and the
// phone read the same two strings, so a pilot pressing the button and a pilot
// reading the app cannot be looking at two different descriptions of the same
// authorisation.
const char* pending_title(Pending pending);
const char* pending_detail(Pending pending);

// INFO: cf 02aug26 A prompt is an open authorisation standing on the glass, so
// it is not allowed to stand there forever: a device left on a wing with an
// unanswered "confirm firmware upload" is a device a stranger can walk up to.
// Long enough to put a phone down and reach the button, and no longer.
constexpr uint32_t kPromptForgetMs = 30u * 1000u;

// INFO: cf 02aug26 A push exists so a tablet does not have to poll a gauge that
// barely moves between polls. Pushing on every millivolt reading would just
// move the polling onto the link instead of removing it, so a push is earned
// only by a step this wide, a charging flip, or a level change - three signals
// a pilot actually cares about, not the ADC's noise floor.
constexpr uint8_t kBatteryPushStepPercent = 5;

// INFO: fc 04aug26 The smallest ATT payload a single-frame reply is built to fit.
// An iOS central commonly settles at ATT_MTU 185, three of which are the
// notification header, so 182 is the narrowest real phone in the field and every
// reply that a pilot's app depends on is sized under it at its worst case rather
// than against a local buffer. ports::kMinimumLinkPayloadBytes (20) is lower still and
// is what BLE guarantees; a link that comes up there gets a counted refusal, not
// a notification the controller will fail.
constexpr int kSmallestSupportedPayloadBytes = 182;

constexpr int kPromptDetailBytes = 128;

class ConfigService {
   public:
    ConfigService(ports::Link& link, ConfigStore& store, ports::Dfu* dfu = nullptr,
                  const timing::SlotTimingStatistics* timing_statistics = nullptr)
        : link_(link), store_(store), dfu_(dfu), timing_statistics_(timing_statistics) {}

    // INFO: cf 02aug26 Ground opens this door, latched in core/flight/ground.h
    void set_flight_state(flight::FlightState state);
    flight::FlightState flight_state() const { return flight_state_; }

    // The durable-write policy's own counters, read out beside the dwell
    // histograms because they are measurements of the same second: a write that
    // could not be placed inside its bound is a stall that landed where the slot
    // map did not budget for one, and it must not be silent. Wired by the product
    // that owns the write (products/skyblip_go/services/config_link.h).
    void set_durable_writes(const timing::DurableWriteWindow* writes) { durable_writes_ = writes; }

    // INFO: fc 18oct26 challenge, register, unlock live in identity_link.cpp, else unknown_command
    void set_identity(IdentityLink* identity) { identity_ = identity; }
    // INFO: fc 18oct26 an activated unit's radio runs, so its product places the page write
    bool unlock_due() const { return identity_ != nullptr && identity_->unlock_due(); }
    // INFO: fc 18oct26 writes the held code if the power rule allows, else drops it as low_power
    void settle_unlock();

    // Drives the upload-window timeout. Called once per App::step().
    void tick(uint32_t now_ms);

    // INFO: fc 25sep26 a reply the link refused with WouldBlock goes out here, and a report resumes
    void resume_replies(uint32_t now_ms);
    // While true the next command waits on the bus: its answer would have nowhere to go.
    bool replying() const { return held_bytes_ > 0 || diagnostics_report_ || timing_report_; }

    // Consulted by the MCUmgr image-upload hook. An unauthenticated SMP
    // transport is what makes the browser update page possible (encrypted GATT
    // characteristics are unreliable under Web Bluetooth), so authorisation
    // lives here instead: an upload is only accepted inside a window that a
    // physical button press opened, while the device is positively on the
    // ground. Rogue firmware is stopped separately, by MCUboot's signature
    // check. This stops a stranger in range wasting the secondary slot.
    bool upload_allowed() const { return upload_window_open_ && on_ground(); }
    void close_upload_window() { upload_window_open_ = false; }
    bool receiving_firmware() const { return upload_window_open_; }

    void on_link_up(const events::LinkUp& event);
    void on_link_down(const events::LinkDown& event);
    void on_rx(const events::RxFrame& frame);

    // Whether anyone is listening. Exported because it is the one fact about
    // this service that no reply reveals until the gauge happens to move, and a
    // product test has to be able to assert that a connection reached it.
    bool link_up() const { return connected_count_ > 0; }
    const LinkClaim& claim() const { return claim_; }
    bool claim_link(uint16_t session_id) { return claim_.grant(session_id); }
    uint16_t claim_holder() const { return claim_.holder(); }

    // The values core/power already decided: state of charge, the millivolt
    // reading, whether the charger is holding the rail, and the level
    // core/power's cutoff rule made of the same samples. This service carries
    // them to the link, it does not re-derive what a low cell is. While a link
    // is up, a charging flip, a level change or a step-sized move in percent
    // pushes an unsolicited status so the tablet's gauge moves without a poll.
    void set_battery_state(const power::BatteryState& battery, power::PowerLevel level);

    // How warm the silicon is, in tenths of a degree, and whether anybody has
    // actually read one. Invalid is not zero: a board with no sensor, a driver
    // that refused a measurement and a device at freezing are three different
    // things, and 0.0 C is a plausible hangar morning - so an invalid reading
    // leaves the key out of the reply altogether rather than publishing a number
    // nobody measured.
    void set_die_temperature(int16_t die_dc, bool valid) {
        diagnostics_.die_dc = die_dc;
        diagnostics_.die_valid = valid;
    }

    // The status dump, held here because this is the one service that can put it
    // on a link (core/comms/diagnostics.h). Everything above writes its own half
    // of it, so the reply, the unsolicited push and the console dump report one
    // set of numbers rather than three copies of them; the product's collector
    // fills the other half through this door, once a second.
    Diagnostics& diagnostics() { return diagnostics_; }
    const Diagnostics& diagnostics() const { return diagnostics_; }

    // Receptions core/traffic refused because the position they claimed was
    // further away than this radio can hear. The number a support case opens
    // with: it says whether the range gate fires once a week or once a second,
    // and the second one is a decoder or a link budget, not a sky full of
    // aircraft.
    void set_range_refused(uint32_t count) { diagnostics_.range_refused = count; }

    // The nRF52840's power-failure comparator has fired, latched by core/power.
    // Kept beside the level because the two feed one rule
    // (core/power/cutoff.h::may_write) and this service is the door that rule
    // has to be enforced at.
    void set_supply_warned(bool warned) { supply_warned_ = warned; }

    // Whether a settings change may be accepted at all. A "set" refused here is
    // refused to the phone, with a reason, rather than accepted and quietly not
    // written: below the low-battery warning the settings sector is not touched,
    // and a companion app that patched a value per keystroke has to be told so.
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

    void set_settings_fallback(settings::Fallback fallback) { settings_fallback_ = fallback; }

    void set_hub_image(const dfu::HubImageReport& report) { hub_image_ = report; }

    // INFO: cf 02aug26 BLE pairing is off on this product (encrypted GATT
    // characteristics break Web Bluetooth on Windows), so physical presence is
    // what stands in for it: nothing sensitive happens without a gesture made
    // on the device itself. These two are the only doors, and only ui/input's
    // confirmation gesture, the prompt timeout and a dropped link may knock.
    void confirm();
    void cancel();

    Pending pending() const { return pending_; }
    const char* prompt_detail() const;
    bool settings_dirty() const { return settings_dirty_; }
    void clear_dirty() { settings_dirty_ = false; }

    // INFO: cf 02aug26 The panel's settings page edits the same struct this
    // service was handed, one validated field at a time, and this is how it
    // says so. There is exactly one writer of the flash blob - the product's
    // ConfigLinkService, on this flag - so the two editors cannot disagree
    // about what is stored. Deliberately not gated on flight state: a "set"
    // from a phone is refused unless the device is positively on the ground,
    // because a phone in a pocket has no proven presence and rewrites the whole
    // struct in one message. A press on the panel is presence itself and moves
    // one field, and alarm volume is a thing a pilot needs in the air.
    void note_settings_changed() { settings_dirty_ = true; }

    // Why the device came up. Read once at boot by the shell, reported here so
    // a watchdog bite in the field is diagnosable without the panel in hand.
    void set_reset_reason(power::ResetReason reason) { diagnostics_.reset = reason; }
    power::ResetReason reset_reason() const { return diagnostics_.reset; }
    void set_went_dark_flat(bool flat) { went_dark_flat_ = flat; }

    // Erasing the flight log destroys evidence a pilot may need for a claim or
    // an incident, so it knocks on the same door a firmware upload does: the
    // flight log service asks, the panel shows the prompt, and only the
    // confirmation gesture on the device itself latches it.
    void request_log_erase();
    bool log_erase_requested() const { return log_erase_requested_; }
    void clear_log_erase_request() { log_erase_requested_ = false; }

    // Latched by a confirmed "gnss_cold_start". A receiver whose almanac is poisoned
    // takes twenty minutes to fix and a pilot reads that as a broken device, so
    // throwing the stored orbit data away has to be reachable without a cable.
    // It is behind the same confirmation as the rest: a cold start costs the next
    // fix, and a phone must not be able to spend that on its own.
    bool gnss_cold_start_requested() const { return gnss_cold_start_requested_; }
    void clear_gnss_cold_start_request() { gnss_cold_start_requested_ = false; }

    // Latched by a confirmed "power_off". The shell owns the sequencer, so this
    // is where the request waits for it: core/power::ShutdownSequencer::request
    // with ShutdownReason::LinkRequest.
    bool power_off_requested() const { return power_off_requested_; }
    void clear_power_off_request() { power_off_requested_ = false; }

    bool install_requested() const { return install_requested_; }
    void clear_install_request() { install_requested_ = false; }

    bool recovery_requested() const { return recovery_requested_; }
    void clear_recovery_request() { recovery_requested_ = false; }

    // INFO: fc 04aug26 Every frame this service could not put on the link: one
    // that would not fit the negotiated payload, one the controller refused, and
    // one a writer left incomplete. Silence towards a phone is a fault worth a
    // number, and the pilot-facing push is the one path that also retries.
    uint32_t link_drops() const { return diagnostics_.link_drops; }

   private:
    Status reply(const uint8_t* frame, int bytes);
    Status reply_to(uint16_t session_id, const uint8_t* frame, int bytes);
    Status deliver(uint16_t session_id, const uint8_t* frame, int bytes);
    Status broadcast(const uint8_t* frame, int bytes);
    void refuse_unclaimed(uint16_t session_id);
    void refuse_too_wide();
    void note_connected(uint16_t session_id);
    void note_disconnected(uint16_t session_id);
    void tally_exchange(const events::LinkDown& event);
    bool connected(uint16_t session_id) const;
    int payload_bytes() const;
    int payload_bytes_to(uint16_t session_id) const;
    void stage(Pending pending, link::Reason reason);
    void acknowledge(bool accepted, std::optional<link::Reason> reason = std::nullopt);
    void send_config();
    void send_status();
    void send_defaults();
    void push_status();
    int format_status(uint8_t* out, int capacity);
    // The bench's plug-in-and-read for G6: the same on_rx dispatch that
    // answers "get" and "status" answers "timing" from the one accumulator
    // core/timing::SlotTimingStatistics keeps, over the link the phone already has
    // open - no second channel, no panel real estate a histogram would not
    // fit on anyway.
    void send_timing();
    // The same door, one more question: what the durable-write policy did with the
    // settings changes it was handed (core/timing/durable_write.h).
    void send_flash();
    // And the radio's own half of the same bench: the dump's radio subsystem, on
    // its own, because a companion page that only draws the air picture should not
    // have to read the receiver's firmware string to get the noise floor. Not four
    // more keys on "status": that reply is the one this service PUSHES
    // unsolicited, and it carries state a pilot's screen reacts to, not counters.
    void send_radio();
    void send_update();
    void push_update(uint16_t session_id);
    int format_update(uint8_t* out, int capacity);
    // And the whole dump, which is the same table as the console's: one frame per
    // subsystem where the payload allows it, more where it does not, and never a
    // frame that mixes two subsystems (core/comms/diagnostics.h).
    void send_diagnostics();
    bool answer_identity(link::Command command, const cbor::Reader& frame);
    void challenge();
    void register_unit(const cbor::Reader& frame);
    void send_registration();
    bool running_version_text(char* out, size_t capacity) const;
    bool identity_refused_in_flight();
    void unlock(const cbor::Reader& frame);
    void refuse(link::Reason reason) { acknowledge(false, reason); }
    template <class Report, class... Arguments>
    void start_report(std::optional<Report>& report, int frame_bytes,
                      const Arguments&... arguments);
    template <class Report>
    void continue_report(std::optional<Report>& report);
    void hold(uint16_t session_id, const uint8_t* frame, int bytes);
    void drop_replies();
    void request_firmware(const cbor::Reader& frame);
    void stage_settings(const events::RxFrame& frame);
    void answer_prompt_request(link::Command command, const cbor::Reader& frame);
    void install_received_image();
    bool signed_by_trusted_key() const;
    bool on_ground() const { return flight_state_ == flight::FlightState::Ground; }
    bool activated() const { return identity_ != nullptr && identity_->activated(); }
    bool register_exempt() const { return pending_ == Pending::Register && !activated(); }

    ports::Link& link_;
    ConfigStore& store_;
    ports::Dfu* dfu_;
    const timing::SlotTimingStatistics* timing_statistics_;
    const timing::DurableWriteWindow* durable_writes_{nullptr};
    IdentityLink* identity_{nullptr};
    flight::FlightState flight_state_{flight::FlightState::Unknown};
    Diagnostics diagnostics_{};
    bool supply_warned_{false};
    bool went_dark_flat_{false};
    uint16_t connected_sessions_[LinkSessions::kMaxSessions]{};
    int connected_count_{0};
    LinkClaim claim_{};
    bool status_push_due_{false};
    bool power_off_requested_{false};
    bool install_requested_{false};
    bool recovery_requested_{false};
    bool log_erase_requested_{false};
    bool gnss_cold_start_requested_{false};
    Pending pending_{Pending::None};
    bool settings_dirty_{false};
    bool upload_window_open_{false};
    uint32_t now_ms_{0};
    uint32_t upload_window_opened_ms_{0};
    uint32_t pending_since_ms_{0};
    ports::ImageVersion approved_version_{};
    char prompt_detail_[kPromptDetailBytes]{};
    uint8_t pending_set_frame_[sizeof(events::RxFrame::data)]{};
    static constexpr int kHeldFrameBytes = kTimingFrameBytes;
    static_assert(kHeldFrameBytes >= DiagnosticsReport::kFrameBytes,
                  "a report frame must be holdable");
    static_assert(kHeldFrameBytes >= IdentityLink::kRegistrationFrameBytes,
                  "a registration frame must be holdable");
    uint8_t held_frame_[kHeldFrameBytes]{};
    int held_bytes_{0};
    uint16_t held_for_session_id_{0};
    uint32_t held_since_ms_{0};
    std::optional<DiagnosticsReport> diagnostics_report_{};
    std::optional<TimingReport> timing_report_{};
    int report_payload_bytes_{0};
    int report_frame_bytes_{0};
    int pending_set_bytes_{0};
    dfu::ImageState image_state_{dfu::ImageState::Confirmed};
    dfu::UpdateRecord update_record_{};
    settings::Fallback settings_fallback_{settings::Fallback::None};
    dfu::HubImageReport hub_image_{};

    // Long enough to upload ~730 KB over BLE on a slow phone, short enough that
    // a device left on a bench does not stay writable all afternoon.
    static constexpr uint32_t kUploadWindowMs = 10u * 60u * 1000u;
};

}

#endif
