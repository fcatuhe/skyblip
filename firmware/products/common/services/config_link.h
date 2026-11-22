#ifndef SKYBLIP_PRODUCTS_COMMON_SERVICES_CONFIG_LINK_H
#define SKYBLIP_PRODUCTS_COMMON_SERVICES_CONFIG_LINK_H

#include "core/comms/install_trust.h"
#include "core/dfu/install_report.h"
#include "core/dfu/update.h"
#include "core/diagnostics/payload.h"
#include "core/identity/activation.h"
#include "core/identity/unit.h"
#include "core/power/cutoff.h"
#include "core/power/duty.h"
#include "core/settings/blob.h"
#include "core/timing/durable_write.h"
#include "products/common/services/config_service.h"
#include "products/common/settings_store.h"
#include "runtime/service.h"

namespace skyblip::common {

// INFO: fc 20nov26 below the low-battery warning a change is refused at the door and a pending one
// is held dirty, never dropped
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
          unit_(context.roles.identity, context.roles.random),
          identity_(unit_) {
        config_.set_durable_writes(&writes_);
        config_.set_identity(&identity_);
        config_.set_product(product);
    }

    Status setup() override;
    void load();
    void load_browsers();
    void adopt_install_trust();
    void load_identity();
    void tick(uint32_t now_ms) override;
    // INFO: fc 19nov26 a reply or a write goes a pass at a time, and a frame wakes the rest
    uint32_t due_in_ms(uint32_t now_ms) const override;

    void set_product_name(const char* product_name) { product_name_ = product_name; }

    ConfigService& config() { return config_; }
    const timing::DurableWriteWindow& durable_writes() const { return writes_; }

    // INFO: fc 20nov26 counted once per change, so the number reads as changes waiting for a
    // charger
    uint32_t refused_writes() const { return refused_; }
    uint32_t failed_writes() const { return failed_; }
    settings::Fallback settings_fallback() const { return fallback_; }
    bool holding_for_power() const { return held_; }

    // INFO: fc 20nov26 the shutdown skips the write window, never the power rule, so a deliberate
    // power-off keeps the pilot's last change
    void flush_settings(uint32_t now_ms);

    void install();

   private:
    void publish_identity(uint32_t now_ms);
    void advertise(uint32_t now_ms);
    void place_unlock(uint32_t now_ms);
    void record_link(diagnostics::LinkAction action, uint16_t session, uint16_t frame_bytes,
                     uint32_t now_ms);
    diagnostics::Link link_record(diagnostics::LinkAction action, uint16_t session,
                                  uint16_t frame_bytes) const;
    void record_link_down(const events::LinkEvent& down, uint32_t now_ms);
    void record_write(const timing::DurableWriteWindow& window, timing::DurableWriteVerdict verdict,
                      uint32_t now_ms);
    void watch_claim(uint32_t now_ms);
    void watch_link_drops(uint32_t now_ms);
    void accrue_connected(uint32_t now_ms);
    void publish_upload_verdict();
    void publish_link();

    // INFO: fc 18oct26 a Bluetooth that never came up is asked once a second, not every pass
    static constexpr uint32_t kAdvertiseRetryPeriodMs = 1000;
    static constexpr size_t kBlobBytes = 64;
    static constexpr const char* kUpdateKey = "update";
    static constexpr const char* kPriorSettingsKey = "settings.prior";
    static constexpr const char* kReportKey = "install.report";

    void drain_link_events(uint32_t now_ms);
    void take_request(uint32_t now_ms);
    void drain_settings(uint32_t now_ms);
    void write_settings(uint32_t now_ms, bool forced);
    void drain_browsers(uint32_t now_ms);
    void write_browsers(uint32_t now_ms, bool forced);
    void drain_install_trust(uint32_t now_ms);
    bool persist();
    bool newer_layout(const uint8_t* blob, size_t blob_bytes) const;
    static bool blob_fits(size_t blob_bytes) { return blob_bytes > 0 && blob_bytes <= kBlobBytes; }
    void load_prior();
    bool adopt(const uint8_t* blob, size_t blob_bytes);
    void remember(const uint8_t* blob, size_t blob_bytes);
    void keep_settings_for_a_revert();
    const char* settings_key() const { return newer_kept_ ? kPriorSettingsKey : kSettingsKey; }
    void load_image_state();
    void record_update(const ports::SwapRequest& request);
    void forget_update();
    void confirm_image_once_healthy();
    dfu::Proof proof() const;
    void publish_image_state();
    void load_report();
    void write_report();
    void note_proof(dfu::Proof reached);
    void forget_report();

    bool storable() const {
        return ports::has(context_.roles.capabilities, ports::Capability::Storage) &&
               context_.roles.key_value.ready();
    }
    bool may_persist() const { return power_.may_write(power::DurableWrite::Settings); }
    bool hold_for_power();

    SettingsStore& store_;
    ConfigService config_;
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
    timing::DurableWriteWindow trust_page_erases_{};
    comms::InstallTrust install_trust_{};
    power::OnTime connected_{};
    uint16_t recorded_holder_{0};
    bool recorded_claim_held_{false};
    uint32_t recorded_drops_{0};
    uint32_t refused_{0};
    uint32_t failed_{0};
    bool held_{false};
    bool loaded_{false};
    // INFO: fc 20nov26 a change stepped back to what flash holds writes nothing, which keeps NVS
    // garbage collection rare
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
    dfu::InstallReport report_{};
    bool reported_{false};
};

}  // namespace skyblip::common

#endif
