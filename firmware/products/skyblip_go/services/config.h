#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_CONFIG_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_CONFIG_H

#include "core/comms/config.h"
#include "core/diag/payload.h"
#include "core/power/cutoff.h"
#include "core/power/duty.h"
#include "core/timing/durable_write.h"
#include "products/skyblip_go/services/power.h"
#include "products/skyblip_go/settings_store.h"
#include "runtime/service.h"

namespace skyblip::go {

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
    ConfigLinkService(runtime::Context& ctx, Settings& settings, const PowerService& power)
        : runtime::Service(ctx),
          settings_(settings),
          store_(settings, ctx.roles.device_addr),
          config_(ctx.roles.link, store_, &ctx.roles.dfu, &ctx.state.rf.timing_stats),
          power_(power) {
        config_.set_durable_writes(&writes_);
    }

    Status setup() override;
    void load();
    void tick(uint32_t now_ms) override;

    comms::ConfigService& config() { return config_; }
    const timing::DurableWriteWindow& durable_writes() const { return writes_; }

    // Changes core/power refused to write, counted once each and not once per
    // pass: one number a bench can read as "a pilot's change is waiting for a
    // charger". Not a fault of the placement policy above - it means something
    // else entirely, which is why it is not one of its counters.
    uint32_t refused_writes() const { return refused_; }
    uint32_t failed_writes() const { return failed_; }
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
    void adopt_learned_trim();
    void spend_gnss_cold_start();
    void record_link(diag::LinkAction action, uint16_t session, uint16_t frame_bytes,
                     uint32_t now_ms);
    void record_write(timing::DurableWriteVerdict verdict, uint32_t now_ms);
    void watch_claim(uint32_t now_ms);
    void watch_link_drops(uint32_t now_ms);
    void accrue_connected(uint32_t now_ms);

    static constexpr size_t kBlobCap = 64;
    static constexpr const char* kUpdateKey = "update";

    void drain_link_events(uint32_t now_ms);
    void take_request(uint32_t now_ms);
    void drain_settings(uint32_t now_ms);
    void write_settings(uint32_t now_ms, bool forced);
    bool persist();
    void load_image_state();
    void forget_update();
    void confirm_image_once_healthy();
    bool hardware_proven() const;
    void publish_image_state();

    // Whether the blob may go to flash at all, asked of the one service that
    // knows what the cell is doing. A reference, not a pointer: the product wires
    // it at construction and there is no version of this device where the
    // question has no owner.
    bool storable() const {
        return ports::has(context_.roles.capabilities, ports::Capability::Storage) &&
               context_.roles.kv.ready();
    }
    bool may_persist() const { return power_.may_write(power::DurableWrite::Settings); }
    bool hold_for_power();

    Settings& settings_;
    SettingsStore store_;
    comms::ConfigService config_;
    const PowerService& power_;
    timing::DurableWriteWindow writes_{};
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
    uint8_t stored_[kBlobCap]{};
    size_t stored_len_{0};
    static constexpr int kConfirmAttempts = 3;
    bool image_confirmed_{false};
    bool image_state_loaded_{false};
    int confirm_attempts_{0};
    dfu::ImageState image_state_{dfu::ImageState::Confirmed};
    dfu::UpdateRecord update_record_{};
    bool update_recorded_{false};
};

}  // namespace skyblip::go

#endif
