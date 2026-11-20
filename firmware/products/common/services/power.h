#ifndef SKYBLIP_PRODUCTS_COMMON_SERVICES_POWER_H
#define SKYBLIP_PRODUCTS_COMMON_SERVICES_POWER_H

#include "core/diagnostics/profile.h"
#include "core/power/battery.h"
#include "core/power/charging.h"
#include "core/power/cutoff.h"
#include "ports/capabilities.h"
#include "ports/die_temperature.h"
#include "runtime/service.h"
#include "runtime/tasks.h"

namespace skyblip::common {

struct PowerSettings {
    PowerSettings(const int16_t& battery_offset_mv_of) : battery_offset_mv(battery_offset_mv_of) {}
    PowerSettings(int16_t&&) = delete;
    PowerSettings(const int16_t&&) = delete;

    const int16_t& battery_offset_mv;
};

// INFO: fc 20nov26 the only writer of state.power.level, so the lamp, the glass and the link never
// re-derive a low cell
class PowerService : public runtime::Service {
   public:
    PowerService(runtime::Context& context, PowerSettings settings)
        : runtime::Service(context), settings_(settings) {}

    void tick(uint32_t now_ms) override;
    uint32_t due_in_ms(uint32_t now_ms) const override;
    void record_last_pass(uint32_t now_ms);

    bool cutoff() const { return cutoff_.cutoff(); }
    uint32_t implausible_samples() const { return gauge_.refused(); }

    void on_supply_warning() { cutoff_.on_supply_warning(); }
    uint32_t supply_warnings() const { return cutoff_.supply_warnings(); }

    const power::CutoffMonitor& cutoff_monitor() const { return cutoff_; }

    power::ChargeCondition charge_condition() const { return charge_; }
    uint32_t charge_warnings() const { return charge_warnings_; }

    // INFO: fc 06sep26 a sensor that stopped answering must neither hold nor drive the glass
    static constexpr uint32_t kDieStaleMs = 30000;

    // INFO: fc 20sep26 the cadence the cell is sampled at, so no record repeats a reading
    static constexpr uint32_t kRecordPeriodMs = runtime::kBatteryPeriodMs;

    // INFO: fc 21sep26 a counter of screen, receiver and buzzer seconds needs no finer grain
    static constexpr uint32_t kDutyRecordPeriodMs = 10000;
    static_assert(kDutyRecordPeriodMs <= diagnostics::kDutyPeriodCeilingMs,
                  "two Duty records this far apart cannot be subtracted");

   private:
    // INFO: fc 05aug26 the die moves in minutes, and each read sleeps on the HFCLK: 10 s is plenty
    static constexpr uint32_t kDiePeriodMs = 10000;
    static_assert(kDieStaleMs >= 2 * kDiePeriodMs, "one missed sample is not a dead sensor");

    // INFO: fc 20nov26 false for ever on a board with no sensor, never a zero, because 0.0 C is a
    // plausible hangar morning
    bool die_reading_fresh(uint32_t now_ms) const {
        return die_valid_ && now_ms - die_valid_ms_ <= kDieStaleMs;
    }
    void sample_die_temperature(uint32_t now_ms);
    void watch_charge();
    void record_pass(uint32_t now_ms);
    void record_power(const diagnostics::Instant& at);
    void record_duty(const diagnostics::Instant& at);
    uint32_t power_period_ms() const;
    uint32_t duty_period_ms() const;

    power::Gauge gauge_{};
    power::CutoffMonitor cutoff_{};
    power::ChargeCondition charge_{power::ChargeCondition::Unknown};
    uint32_t charge_warnings_{0};
    uint32_t recorded_ms_{0};
    uint32_t duty_recorded_ms_{0};
    uint32_t die_read_ms_{0};
    uint32_t die_valid_ms_{0};
    int16_t die_dc_{0};
    bool die_valid_{false};
    bool die_sampled_{false};
    PowerSettings settings_;
};

}  // namespace skyblip::common

#endif
