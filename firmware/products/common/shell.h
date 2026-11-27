#ifndef SKYBLIP_PRODUCTS_COMMON_SHELL_H
#define SKYBLIP_PRODUCTS_COMMON_SHELL_H

#include <algorithm>
#include <optional>

#include "core/power/battery.h"
#include "core/power/cable.h"
#include "core/power/reset_reason.h"
#include "core/power/shutdown.h"
#include "core/power/wake.h"
#include "products/common/services/capture.h"
#include "products/common/services/config_service.h"
#include "runtime/loop_meter.h"

namespace skyblip::common {

template <class Product>
class Shell {
   public:
    Shell(Product& product, uint32_t radio_park_ceiling_ms)
        : product_(product),
          meter_(product.roles_.clock),
          radio_park_ceiling_ms_(radio_park_ceiling_ms) {}

    power::ResetCause read_reset() {
        const power::ResetCause causes = product_.platform_.system_power().reset_causes();
        reset_reason_ = power::classify(causes);
        product_.config_link_.config().set_reset_reason(reset_reason_);
        return causes;
    }

    power::BootPath read_boot(power::ResetCause causes, int16_t battery_offset_mv) {
        boot_cell_ = read_boot_cell(battery_offset_mv);
        boot_path_ = power::boot_path(causes, product_.platform_.button_down(), boot_cell_,
                                      product_.power_switch());
        return boot_path_;
    }

    bool judge(Status board, ports::Capabilities required) {
        flyable_ =
            board == Status::Ok &&
            ports::without(required, product_.board_.capabilities()) == ports::Capability::None;
        return flyable_;
    }

    Status start(Status board) {
        // INFO: fc 25nov26 an install restarts the unit, so the image it boots never saw the cable
        if (product_.config_link_.config().image_state() != dfu::ImageState::Confirmed)
            cable_.arm();
        if (board != Status::Ok) return board;
        if (!flyable_) return Status::Down;
        if (reset_reason_ == power::ResetReason::LowPowerWake ||
            reset_reason_ == power::ResetReason::ChargerWake)
            product_.alarm_.sign_on();
        const Status loop = product_.loop_.setup();
        product_.state_.started = loop == Status::Ok;
        return loop;
    }

    bool refusing() const { return !flyable_ && !shutdown_.going_down(); }

    bool serve(uint32_t now_ms) {
        if (refusing()) guard_cell(now_ms);
        if (!flyable_ || shutdown_.going_down()) return false;
        const bool metered = product_.diagnostics_.armed();
        if (metered) {
            meter_.begin_pass(product_.context_.instant(now_ms).into_ms);
            meter_.timed(diagnostics::LoopService::BoardPoll,
                         [&] { product_.board_.poll(product_.state_, now_ms); });
        } else {
            meter_.rest();
            product_.board_.poll(product_.state_, now_ms);
        }
        // INFO: fc 19nov26 polled before the services, so a pass that may write flash knows first
        if (product_.platform_.system_power().take_supply_warning())
            product_.power_.on_supply_warning();
        product_.loop_.run_pass(now_ms, metered ? &meter_ : nullptr);
        if (metered) meter_.end_pass();
        record_loop(now_ms);
        if (product_.power_.cutoff()) shutdown_.request(power::ShutdownReason::LowBattery, now_ms);
        watch_cable(now_ms);
        take_confirmed(now_ms);
        return true;
    }

    void wind_down(uint32_t now_ms, bool pad_down) {
        // INFO: fc 13nov26 before drive_shutdown, so the sign starts after the park's flash work
        if (shutdown_.going_down()) finish_sign_off(now_ms);
        shutdown_.tick(now_ms, product_.platform_.button_down(), pad_down);
        if (shutdown_.going_down() && !product_.platform_.external_power())
            parked_off_cable_ = true;
        drive_shutdown(now_ms);
        if (shutdown_.going_down()) {
            // INFO: fc 26nov26 the park stops the link's tick, so a held reply goes out here
            product_.config_link_.config().resume_replies(now_ms);
            product_.settle_glass(now_ms);
            await_radio_park(now_ms, ready_but_for_radio());
        }
        enter_recovery_once_parked();
    }

    // INFO: fc 13nov26 a refused boot parks these at once, a switch-off its outputs after the sign
    void park_parts(uint32_t now_ms) {
        product_.park_radio();
        park_outputs(now_ms);
    }

    // INFO: fc 08nov26 a radio that never parks must not keep a switched-off unit awake
    void await_radio_park(uint32_t now_ms, bool waiting) {
        if (!waiting || product_.radio_part().parked()) {
            radio_waited_since_ms_.reset();
            return;
        }
        if (!radio_waited_since_ms_) radio_waited_since_ms_ = now_ms;
        if (now_ms - *radio_waited_since_ms_ >= radio_park_ceiling_ms_)
            product_.radio_part().abandon();
    }

    // INFO: fc 12sep26 rails cut mid-frame leave the ink half-driven, and the sun develops it
    bool ready_to_power_off() const {
        return ready_but_for_radio() && product_.radio_part().parked();
    }

    // INFO: fc 12nov26 a deliberate shutdown is fed through, so a held button never resets the unit
    bool may_feed_watchdog(uint32_t now_ms) const {
        if (!flyable_ || shutdown_.going_down()) return true;
        return product_.loop_.may_feed_watchdog(now_ms);
    }

    // INFO: fc 19nov26 a refusal or a shutdown keeps the pass cadence its parking is timed in
    uint32_t due_in_ms(uint32_t now_ms) const {
        if (!flyable_ || shutdown_.going_down()) return runtime::kServicePassCeilingMs;
        return std::min(product_.loop_.due_in_ms(now_ms),
                        product_.board_.due_in_ms(product_.state_.clock, now_ms));
    }

    // INFO: fc 26nov26 only VBUS rising wakes SYSTEM OFF, so a cable back during the park restarts
    bool cable_returned() const {
        // INFO: fc 26nov26 CableGone trusts a stale battery sample, so the cable may be back
        const bool cable_left =
            parked_off_cable_ || shutdown_.reason() == power::ShutdownReason::CableGone;
        return product_.power_switch() == power::Switch::Cable && !recovering() && cable_left &&
               product_.platform_.external_power();
    }

    bool flyable() const { return flyable_; }
    power::ResetReason reset_reason() const { return reset_reason_; }
    power::BootPath boot_path() const { return boot_path_; }
    const power::BootCell& boot_cell() const { return boot_cell_; }
    power::ShutdownSequencer& shutdown() { return shutdown_; }
    const power::ShutdownSequencer& shutdown() const { return shutdown_; }

   private:
    power::BootCell read_boot_cell(int16_t battery_offset_mv) {
        power::BootCell cell{};
        if (!ports::has(product_.board_.capabilities(), ports::Capability::Battery)) return cell;
        uint16_t raw_mv = 0;
        cell.valid = product_.platform_.read_divider_mv(raw_mv);
        cell.cell_mv = power::calibrated_mv(raw_mv, battery_offset_mv);
        cell.external_power = product_.platform_.external_power();
        return cell;
    }

    void guard_cell(uint32_t now_ms) {
        product_.board_.poll_battery(now_ms);
        product_.power_.tick(now_ms);
        if (product_.power_.cutoff()) shutdown_.request(power::ShutdownReason::LowBattery, now_ms);
        watch_cable(now_ms);
    }

    void watch_cable(uint32_t now_ms) {
        std::optional<uint32_t>& off_in_ms = product_.state_.power.cable_off_in_ms;
        off_in_ms.reset();
        if (product_.power_switch() != power::Switch::Cable) return;
        const bool kept_on =
            product_.state_.flight.confirmed_state == flight::FlightState::Airborne ||
            product_.config_link_.config().updating();
        if (cable_.gone(now_ms, product_.state_.power.battery.external_power, kept_on))
            shutdown_.request(power::ShutdownReason::CableGone, now_ms);
        off_in_ms = cable_.off_in_ms(now_ms);
    }

    void record_loop(uint32_t now_ms) {
        const diagnostics::Instant at = product_.context_.instant(now_ms);
        if (at.at_s == loop_window_s_) return;
        loop_window_s_ = at.at_s;
        const diagnostics::Loop window = meter_.take();
        if (product_.diagnostics_.armed()) product_.diagnostics_.record(window, at);
    }

    void take_confirmed(uint32_t now_ms) {
        const std::optional<Confirmed> confirmed = product_.config_link_.config().take_confirmed();
        if (!confirmed) return;
        switch (confirmed->action) {
            case Action::PowerOff:
                shutdown_.request(power::ShutdownReason::LinkRequest, now_ms);
                break;
            case Action::Install: shutdown_.request(power::ShutdownReason::Install, now_ms); break;
            case Action::Recovery:
                shutdown_.request(power::ShutdownReason::Recovery, now_ms);
                break;
            case Action::EraseLog: product_.flight_log_.erase(confirmed->peer); break;
            case Action::GnssColdStart:
                product_.roles_.gnss.request_restart(ports::Restart::Cold);
                break;
            case Action::Capture: order_capture(product_.diagnostics_, confirmed->capture); break;
        }
    }

    void drive_shutdown(uint32_t now_ms) {
        const power::ShutdownPhase phase = shutdown_.phase();
        if (phase == acted_phase_) return;
        acted_phase_ = phase;
        if (phase == power::ShutdownPhase::Off && installing()) {
            product_.config_link_.install();
            return;
        }
        if (phase != power::ShutdownPhase::Parking) return;
        // INFO: fc 26nov26 the MCUmgr thread reads the gate until the reboot, and no pass closes it
        product_.roles_.dfu.publish_upload_allowed(false);
        product_.park_radio();
        if (power::ends_switched_off(shutdown_.reason(), recovery_powers_off()))
            product_.alarm_.sign_off(now_ms);
        else
            park_outputs(now_ms);
        // INFO: fc 19nov26 the loop stops here, so a settings write waiting for a phase goes now
        product_.config_link_.flush_settings(now_ms);
        product_.power_.record_last_pass(now_ms);
        product_.capture_.park(now_ms);
        product_.park_glass();
    }

    // INFO: fc 19nov26 the loop no longer runs, so each output's owner parks it, haptic included
    void park_outputs(uint32_t now_ms) {
        product_.alarm_.park(now_ms);
        product_.board_.park();
    }

    void finish_sign_off(uint32_t now_ms) {
        if (!product_.alarm_.signing()) return;
        product_.alarm_.advance_sign(now_ms);
        if (!product_.alarm_.signing()) park_outputs(now_ms);
    }

    bool installing() const { return shutdown_.reason() == power::ShutdownReason::Install; }
    bool recovering() const { return shutdown_.reason() == power::ShutdownReason::Recovery; }

    bool recovery_powers_off() const {
        return product_.roles_.dfu.recovery_path() == ports::RecoveryPath::PowerOffToFinish;
    }

    bool ready_but_for_radio() const {
        return shutdown_.ready_to_power_off() && !installing() && !product_.glass_parking() &&
               !product_.alarm_.signing() &&
               (!recovering() || recovery_taken_ == ports::RecoveryPath::PowerOffToFinish);
    }

    void enter_recovery_once_parked() {
        if (!recovering() || recovery_taken_ || !shutdown_.ready_to_power_off()) return;
        if (product_.glass_parking()) return;
        recovery_taken_ = product_.roles_.dfu.enter_recovery();
    }

    Product& product_;
    runtime::LoopMeter meter_;
    const uint32_t radio_park_ceiling_ms_;
    uint32_t loop_window_s_{0};
    power::ShutdownSequencer shutdown_{};
    power::CableWatch cable_{};
    power::ShutdownPhase acted_phase_{power::ShutdownPhase::Running};
    power::ResetReason reset_reason_{power::ResetReason::Unknown};
    power::BootPath boot_path_{power::BootPath::Run};
    power::BootCell boot_cell_{};
    std::optional<ports::RecoveryPath> recovery_taken_{};
    std::optional<uint32_t> radio_waited_since_ms_{};
    bool flyable_{false};
    bool parked_off_cable_{false};
};

}  // namespace skyblip::common

#endif
