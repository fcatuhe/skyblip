#include "products/common/services/power.h"

#include <algorithm>

#include "core/diagnostics/payload.h"
#include "core/events/sensor.h"

namespace skyblip::common {

// INFO: fc 20nov26 gated on the capability, so a board with no sensor never calls the driver
void PowerService::sample_die_temperature(uint32_t now_ms) {
    if (!ports::has(context_.roles.capabilities, ports::Capability::DieTemperature)) return;
    // INFO: fc 20nov26 unsigned subtraction, so the counter's 49.7-day wrap costs one late reading
    if (die_sampled_ && now_ms - die_read_ms_ < kDiePeriodMs) return;
    die_sampled_ = true;
    die_read_ms_ = now_ms;
    int16_t die_dc = 0;
    // INFO: fc 20nov26 a refused read leaves the last good reading standing, never a zero
    if (!context_.roles.die_temperature.read(die_dc)) return;
    die_dc_ = die_dc;
    die_valid_ = true;
    die_valid_ms_ = now_ms;
}

void PowerService::watch_charge() {
    const power::ChargeCondition previous = charge_;
    const bus::PowerState& published = context_.state.power;
    charge_ = power::charge_condition(published.battery.external_power, published.die_valid,
                                      published.die_dc);
    const bool out_of_window =
        charge_ == power::ChargeCondition::TooCold || charge_ == power::ChargeCondition::TooHot;
    if (out_of_window && charge_ != previous) charge_warnings_++;
    context_.state.power.charge = charge_;
}

void PowerService::tick(uint32_t now_ms) {
    events::BatterySample raw{};
    while (context_.bus.battery.pop(raw)) {
        // INFO: fc 20nov26 trimmed once here, so the gauge and the cutoff monitor read the same
        // calibrated sample
        const events::BatterySample sample = power::calibrated(raw, settings_.battery_offset_mv);
        gauge_.apply(sample);
        // INFO: fc 05oct26 a refused reading is no step on the ladder, the cable still overrides it
        if (sample.external_power || power::plausible_mv(sample.divider_mv)) cutoff_.apply(sample);
    }
    context_.state.power.battery = gauge_.state();
    context_.state.power.level = cutoff_.level();
    context_.state.power.supply_warned = cutoff_.supply_warned();
    sample_die_temperature(now_ms);
    context_.state.power.die_dc = die_dc_;
    context_.state.power.die_valid = die_reading_fresh(now_ms);
    watch_charge();
    record_pass(now_ms);
}

uint32_t PowerService::due_in_ms(uint32_t now_ms) const {
    const uint32_t record_ms = runtime::remaining_ms(now_ms - recorded_ms_, power_period_ms());
    if (!ports::has(context_.roles.capabilities, ports::Capability::DieTemperature))
        return record_ms;
    return std::min(record_ms, runtime::remaining_ms(now_ms - die_read_ms_, kDiePeriodMs));
}

void PowerService::record_pass(uint32_t now_ms) {
    if (now_ms - recorded_ms_ < power_period_ms()) return;
    recorded_ms_ = now_ms;
    if (!context_.diagnostics.armed()) return;

    const diagnostics::Instant at = context_.instant(now_ms);
    record_power(at);
    if (now_ms - duty_recorded_ms_ < duty_period_ms()) return;
    duty_recorded_ms_ = now_ms;
    record_duty(at);
}

void PowerService::record_last_pass(uint32_t now_ms) {
    if (!context_.diagnostics.armed()) return;
    const diagnostics::Instant at = context_.instant(now_ms);
    record_power(at);
    record_duty(at);
}

void PowerService::record_power(const diagnostics::Instant& at) {
    const bus::PowerState& power = context_.state.power;
    diagnostics::Power value{};
    value.cell_mv = power.battery.cell_mv;
    value.sample_offset_mv =
        diagnostics::sample_offset_mv(power.battery.sample_mv, power.battery.cell_mv);
    value.supply_warnings = cutoff_.supply_warnings();
    value.implausible = gauge_.refused();
    value.charge_warnings = charge_warnings_;
    value.die_dc = power.die_dc;
    value.percent = power.battery.percent;
    value.power_level = power.level;
    value.charge = power.charge;
    value.charging = power.battery.charging;
    value.external_power = power.battery.external_power;
    value.battery_valid = power.battery.valid;
    value.die_valid = power.die_valid;
    context_.diagnostics.record(value, at);
}

void PowerService::record_duty(const diagnostics::Instant& at) {
    const bus::DutyState& duty = context_.state.duty;
    diagnostics::Duty value{};
    value.panel_partial_refreshes = duty.panel_partial_refreshes;
    value.panel_full_refreshes = duty.panel_full_refreshes;
    value.backlight_ms = duty.backlight_ms;
    value.rx_armed_ms = duty.rx_armed_ms;
    value.tx_keyed_ms = duty.tx_keyed_ms;
    value.ble_connected_ms = duty.ble_connected_ms;
    value.annunciator_ms = duty.annunciator_ms;
    context_.diagnostics.record(value, at);
}

uint32_t PowerService::power_period_ms() const {
    return diagnostics::paced(context_.diagnostics.profile()) ? diagnostics::kPowerRunRecordPeriodMs
                                                              : kRecordPeriodMs;
}

uint32_t PowerService::duty_period_ms() const {
    return diagnostics::paced(context_.diagnostics.profile()) ? diagnostics::kPowerRunRecordPeriodMs
                                                              : kDutyRecordPeriodMs;
}

}  // namespace skyblip::common
