#ifndef SKYBLIP_PRODUCTS_COMMON_SERVICES_CONFIG_LINK_H
#define SKYBLIP_PRODUCTS_COMMON_SERVICES_CONFIG_LINK_H

#include "core/comms/config.h"
#include "core/dfu/update.h"
#include "core/diagnostics/payload.h"
#include "core/identity/activation.h"
#include "core/identity/unit.h"
#include "core/power/cutoff.h"
#include "core/power/duty.h"
#include "core/settings/blob.h"
#include "core/timing/durable_write.h"
#include "products/common/settings_store.h"
#include "runtime/service.h"

namespace skyblip::common {

// The companion link's device side: load and persist settings, feed the config
// state machine, and take the running image off probation once the hardware has
// proved itself.
//
// The persist half is why core/timing::DurableWriteWindow exists. A settings
// change is now a request, not a call: this service is the only writer of the
// blob, so it is also the only place that has to place the NVMC stall somewhere
// the radio can afford it. The pilot's side of that is invisible - the panel has
// already shown the new value, because state.settings changed when they changed
// it; what waits is the flash.
//
// The other half is core/power's write rule. Below the low-battery warning the
// settings sector is not touched: NVS survives an interrupted write by design,
// but the sector it garbage-collects is the internal flash the running image
// executes from, and the moment a write lands is the moment a 14 dBm burst sags a
// 3.3 V cell. So a change arriving from a phone is refused at the door, out loud,
// with the reason on the link (core/comms/config.cpp), and a change already
// pending when the cell falls through the warning is HELD rather than dropped: it
// stays dirty, so a charger arriving still saves it, and the refusals are counted
// and readable over the same link. What is lost is one setting a pilot can make
// again; what is protected is every setting they ever made.
class ConfigLinkService : public runtime::Service {
   public:
    static constexpr const char* kSettingsKey = "settings";
    static constexpr const char* kBrowsersKey = "browsers";

    ConfigLinkService(runtime::Context& context, SettingsStore& store,
                      const power::CutoffMonitor& power, const char* product_name,
                      identity::NameRule name_rule, dfu::Product product)
        : runtime::Service(context),
          store_(store),
          config_(context.roles.link, store_, &context.roles.dfu,
                  &context.state.rf.timing_statistics),
          power_(power),
          product_name_(product_name),
          name_rule_(name_rule),
          unit_(context.roles.identity),
          identity_(unit_) {
        config_.set_durable_writes(&writes_);
        config_.set_identity(&identity_);
        config_.set_product(product);
    }

    Status setup() override;
    void load();
    void load_browsers();
    void load_identity();
    void tick(uint32_t now_ms) override;

    void set_product_name(const char* product_name) { product_name_ = product_name; }

    comms::ConfigService& config() { return config_; }
    const timing::DurableWriteWindow& durable_writes() const { return writes_; }

    // Changes core/power refused to write, counted once each and not once per
    // pass: one number a bench can read as "a pilot's change is waiting for a
    // charger". Not a fault of the placement policy above - it means something
    // else entirely, which is why it is not one of its counters.
    uint32_t refused_writes() const { return refused_; }
    uint32_t failed_writes() const { return failed_; }
    settings::Fallback settings_fallback() const { return fallback_; }
    bool holding_for_power() const { return held_; }

    // The one caller that may skip the WINDOW - not the power rule above, which
    // no caller skips: the shutdown sequencer, once the radio has been aborted.
    // The bound above answers the cell that dies without warning; this answers
    // the power-off that does give warning, because losing a change a pilot made
    // to a deliberate shutdown would be the same broken promise for no reason at
    // all.
    void flush_settings(uint32_t now_ms);

    // INFO: fc 07sep26 written in the same pass as trigger(), so no reboot can fall between them
    void record_update();

   private:
    void publish_identity(uint32_t now_ms);
    void advertise(uint32_t now_ms);
    void place_unlock(uint32_t now_ms);
    void spend_gnss_cold_start();
    void record_link(diagnostics::LinkAction action, uint16_t session, uint16_t frame_bytes,
                     uint32_t now_ms);
    diagnostics::Link link_record(diagnostics::LinkAction action, uint16_t session,
                                  uint16_t frame_bytes) const;
    void record_link_down(const events::LinkEvent& down, uint32_t now_ms);
    void record_write(timing::DurableWriteVerdict verdict, uint32_t now_ms);
    void watch_claim(uint32_t now_ms);
    void watch_link_drops(uint32_t now_ms);
    void accrue_connected(uint32_t now_ms);
    void publish_upload_verdict();
    void publish_link();

    // INFO: fc 18oct26 a Bluetooth that never came up is asked, and logs its refusal, once a second
    static constexpr uint32_t kAdvertiseRetryPeriodMs = 1000;
    static constexpr size_t kBlobBytes = 64;
    static constexpr const char* kUpdateKey = "update";
    static constexpr const char* kPriorSettingsKey = "settings.prior";

    void drain_link_events(uint32_t now_ms);
    void take_request(uint32_t now_ms);
    void drain_settings(uint32_t now_ms);
    void write_settings(uint32_t now_ms, bool forced);
    void drain_browsers(uint32_t now_ms);
    void write_browsers(uint32_t now_ms, bool forced);
    bool persist();
    bool newer_layout(const uint8_t* blob, size_t blob_bytes) const;
    static bool blob_fits(size_t blob_bytes) { return blob_bytes > 0 && blob_bytes <= kBlobBytes; }
    void load_prior();
    bool adopt(const uint8_t* blob, size_t blob_bytes);
    void remember(const uint8_t* blob, size_t blob_bytes);
    void keep_settings_for_a_revert();
    const char* settings_key() const { return newer_kept_ ? kPriorSettingsKey : kSettingsKey; }
    void load_image_state();
    void forget_update();
    void confirm_image_once_healthy();
    bool hardware_proven() const;
    void publish_image_state();

    bool storable() const {
        return ports::has(context_.roles.capabilities, ports::Capability::Storage) &&
               context_.roles.key_value.ready();
    }
    // Whether the blob may go to flash at all, asked of the cutoff monitor the
    // power service owns. A reference, not a pointer: the product wires it at
    // construction and there is no version of this device where the question
    // has no owner.
    bool may_persist() const { return power_.may_write(power::DurableWrite::Settings); }
    bool hold_for_power();

    SettingsStore& store_;
    comms::ConfigService config_;
    const power::CutoffMonitor& power_;
    const char* product_name_;
    identity::NameRule name_rule_;
    identity::Unit unit_;
    comms::IdentityLink identity_;
    bool identity_published_{false};
    bool advertised_{false};
    bool advertise_asked_{false};
    uint32_t advertise_asked_ms_{0};
    // INFO: fc 18oct26 an activated unit's unlock page is placed like the settings, outside a dwell
    timing::DurableWriteWindow unlock_writes_{};
    timing::DurableWriteWindow writes_{};
    timing::DurableWriteWindow browser_writes_{};
    power::OnTime connected_{};
    uint16_t recorded_holder_{0};
    bool recorded_claim_held_{false};
    uint32_t recorded_drops_{0};
    uint32_t refused_{0};
    uint32_t failed_{0};
    bool held_{false};
    bool loaded_{false};
    // The blob as flash already holds it. A pilot who steps a value up and back
    // down again has changed nothing, and NVS charges for a write either way:
    // this is what makes that free, and what keeps the sector - and so the
    // garbage collection the whole budget above is sized against - from filling
    // for no reason.
    uint8_t stored_[kBlobBytes]{};
    size_t stored_bytes_{0};
    bool newer_kept_{false};
    settings::Fallback fallback_{settings::Fallback::None};
    static constexpr int kConfirmAttempts = 3;
    bool image_confirmed_{false};
    bool image_state_loaded_{false};
    int confirm_attempts_{0};
    dfu::ImageState image_state_{dfu::ImageState::Confirmed};
    dfu::UpdateRecord update_record_{};
    bool update_recorded_{false};
};

}  // namespace skyblip::common

#endif
