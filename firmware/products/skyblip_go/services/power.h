#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_POWER_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_POWER_H

#include "core/diag/profile.h"
#include "core/power/battery.h"
#include "core/power/charging.h"
#include "core/power/cutoff.h"
#include "core/power/trim.h"
#include "ports/capabilities.h"
#include "ports/die_temperature.h"
#include "products/skyblip_go/settings.h"
#include "runtime/service.h"
#include "runtime/tasks.h"

namespace skyblip::go {

// Owns state.power.battery and state.power.level: what the divider read becomes the
// voltage and the state of charge every screen and the companion link report,
// and what core/power's cutoff rule made of the same samples. The level is
// published rather than re-derived downstream, so the one place that knows the
// cell is nearly gone is the one place that says so.
class PowerService : public runtime::Service {
   public:
    PowerService(runtime::Context& context, const Settings& settings)
        : runtime::Service(context), settings_(settings) {}

    void tick(uint32_t now_ms) override;
    void record_last_pass(uint32_t now_ms);

    bool cutoff() const { return cutoff_.cutoff(); }
    uint32_t implausible_samples() const { return cutoff_.implausible(); }

    // The power-failure comparator fired. Polled off ports::SystemPower by the
    // product and handed here, because this service owns the cutoff monitor and
    // the monitor is where the rule lives (core/power/cutoff.h).
    void on_supply_warning() { cutoff_.on_supply_warning(); }
    bool supply_warned() const { return cutoff_.supply_warned(); }
    uint32_t supply_warnings() const { return cutoff_.supply_warnings(); }

    // Whether a durable write of this kind may happen now. One reader today, the
    // settings writer; the flight log's answer is in the same rule and is always
    // yes, which is the point of asking through it rather than around it.
    bool may_write(power::DurableWrite kind) const { return cutoff_.may_write(kind); }

    // Tenths of a degree, and whether a reading younger than kDieStaleMs stands:
    // the one freshness the panel, the charge window and the reply all read.
    // False on a board with no sensor for ever, which is what the reply reads to
    // decide whether the key exists at all - never a zero standing in for absent,
    // because 0.0 C is a plausible hangar morning.
    bool die_temperature_valid() const { return context_.state.power.die_valid; }
    int16_t die_temperature_dc() const { return context_.state.power.die_dc; }

    power::ChargeCondition charge_condition() const { return charge_; }
    uint32_t charge_warnings() const { return charge_warnings_; }

    bool trim_learned() const { return trim_.learned(); }
    int16_t learned_offset_mv() const { return trim_.offset_mv(); }

    // INFO: fc 06sep26 a sensor that stopped answering must neither hold nor drive the glass
    static constexpr uint32_t kDieStaleMs = 30000;
    bool die_reading_fresh(uint32_t now_ms) const {
        return die_valid_ && now_ms - die_valid_ms_ <= kDieStaleMs;
    }

    // INFO: fc 20sep26 the cadence the cell is sampled at, so no record repeats a reading
    static constexpr uint32_t kRecordPeriodMs = runtime::kBatteryPeriodMs;

    // INFO: fc 21sep26 a counter of screen, receiver and buzzer seconds needs no finer grain
    static constexpr uint32_t kDutyRecordPeriodMs = 10000;
    static_assert(kDutyRecordPeriodMs <= diag::kDutyMaxPeriodMs,
                  "two Duty records this far apart cannot be subtracted");

   private:
    // INFO: fc 05aug26 Die temperature moves in minutes: it is the temperature of
    // a lump of plastic in the sun, low-passed by its own mass. Ten seconds is
    // already generous, so the cheapest correct cadence is the one to take - and
    // the measurement is not free. Zephyr's temp_nrf5 sample_fetch takes a mutex,
    // requests the HFCLK through onoff and then BLOCKS on the DATARDY semaphore
    // (drivers/sensor/nordic/temp/temp_nrf5.c:41-75), so it is a sleeping call and
    // may not be made from an interrupt or from the radio thread. It runs on the
    // service pass, where the executor that owns the dwells outranks it.
    static constexpr uint32_t kDiePeriodMs = 10000;
    static_assert(kDieStaleMs >= 2 * kDiePeriodMs, "one missed sample is not a dead sensor");

    void sample_die_temperature(uint32_t now_ms);
    void watch_charge();
    void record_pass(uint32_t now_ms);
    void record_power(const diag::Instant& at);
    void record_duty(const diag::Instant& at);
    uint32_t power_period_ms() const;
    uint32_t duty_period_ms() const;

    power::Gauge gauge_{};
    power::CutoffMonitor cutoff_{};
    power::FloatTrim trim_{};
    power::ChargeCondition charge_{power::ChargeCondition::Unknown};
    uint32_t charge_warnings_{0};
    uint32_t recorded_ms_{0};
    uint32_t duty_recorded_ms_{0};
    uint32_t die_read_ms_{0};
    uint32_t die_valid_ms_{0};
    int16_t die_dc_{0};
    bool die_valid_{false};
    bool die_sampled_{false};
    const Settings& settings_;
};

}  // namespace skyblip::go

#endif
