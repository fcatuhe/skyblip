#include "products/skyblip_go/services/power.h"

#include "core/diag/payload.h"
#include "core/events/sensor.h"

namespace skyblip::go {

// Gated on the capability and not on the port's own answer, the same way the
// board gates the barometer and the divider it polls: a part the platform never
// found is a part nobody asks. The port would answer false anyway - this is what
// keeps a device that has no sensor from calling into a driver once every ten
// seconds to be told so.
void PowerService::sample_die_temperature(uint32_t now_ms) {
    if (!ports::has(context_.roles.capabilities, ports::Capability::DieTemperature)) return;
    // Unsigned subtraction, so the 49.7-day wrap of the millisecond counter costs
    // one late reading and not a service that never reads again.
    if (die_sampled_ && now_ms - die_read_ms_ < kDiePeriodMs) return;
    die_sampled_ = true;
    die_read_ms_ = now_ms;
    int16_t decicelsius = 0;
    // A refused measurement leaves the last good one standing rather than
    // publishing a zero: the reading is minutes old by design anyway.
    if (!context_.roles.die_temperature.read(decicelsius)) return;
    die_dc_ = decicelsius;
    die_valid_ = true;
    die_valid_ms_ = now_ms;
}

void PowerService::watch_charge() {
    const power::ChargeCondition was = charge_;
    const bus::PowerState& published = context_.state.power;
    charge_ = power::charge_condition(published.battery.external_power, published.die_valid,
                                      published.die_dc);
    const bool out_of_window =
        charge_ == power::ChargeCondition::TooCold || charge_ == power::ChargeCondition::TooHot;
    if (out_of_window && charge_ != was) charge_warnings_++;
    context_.state.power.charge = charge_;
}

void PowerService::tick(uint32_t now_ms) {
    events::BatterySample raw{};
    while (context_.bus.battery.pop(raw)) {
        // Trimmed once, here, on the way out of the queue: the gauge and the
        // cutoff monitor are separate objects fed from the same stream, and a
        // cutoff that fired 40 mV early on a trimmed unit would be the
        // calibration causing the failure it exists to prevent. See
        // core/power/battery.h for what the offset is and where it comes from.
        trim_.apply(raw, now_ms);
        const events::BatterySample sample = power::calibrated(raw, settings_.battery_offset_mv);
        gauge_.apply(sample);
        cutoff_.apply(sample);
    }
    context_.state.power.battery = gauge_.state();
    context_.state.power.level = cutoff_.level();
    context_.state.power.caution = cutoff_.caution();
    context_.state.power.supply_warned = cutoff_.supply_warned();
    sample_die_temperature(now_ms);
    context_.state.power.die_dc = die_dc_;
    context_.state.power.die_valid = die_reading_fresh(now_ms);
    watch_charge();
    record_pass(now_ms);
}

void PowerService::record_pass(uint32_t now_ms) {
    if (now_ms - recorded_ms_ < power_period_ms()) return;
    recorded_ms_ = now_ms;
    if (!context_.diag.armed()) return;

    const diag::Instant at = context_.instant(now_ms);
    record_power(at);
    if (now_ms - duty_recorded_ms_ < duty_period_ms()) return;
    duty_recorded_ms_ = now_ms;
    record_duty(at);
}

void PowerService::record_last_pass(uint32_t now_ms) {
    if (!context_.diag.armed()) return;
    const diag::Instant at = context_.instant(now_ms);
    record_power(at);
    record_duty(at);
}

void PowerService::record_power(const diag::Instant& at) {
    const bus::PowerState& power = context_.state.power;
    diag::Power value{};
    value.cell_mv = power.battery.millivolts;
    value.supply_warnings = cutoff_.supply_warnings();
    value.implausible = cutoff_.implausible();
    value.charge_warnings = charge_warnings_;
    value.die_dc = power.die_dc;
    value.trim_offset_mv = trim_.offset_mv();
    value.percent = power.battery.percent;
    value.level = power.level;
    value.charge = power.charge;
    value.charging = power.battery.charging;
    value.external_power = power.battery.external_power;
    value.valid = power.battery.valid;
    value.die_valid = power.die_valid;
    value.caution = power.caution;
    value.trim_learned = trim_.learned();
    context_.diag.record(value, at);
}

void PowerService::record_duty(const diag::Instant& at) {
    const bus::DutyState& duty = context_.state.duty;
    diag::Duty value{};
    value.panel_partial_refreshes = duty.panel_partial_refreshes;
    value.panel_full_refreshes = duty.panel_full_refreshes;
    value.backlight_ms = duty.backlight_ms;
    value.rx_armed_ms = duty.rx_armed_ms;
    value.tx_keyed_ms = duty.tx_keyed_ms;
    value.ble_connected_ms = duty.ble_connected_ms;
    value.annunciator_ms = duty.annunciator_ms;
    context_.diag.record(value, at);
}

uint32_t PowerService::power_period_ms() const {
    return context_.diag.profile() == diag::Profile::PowerRun ? diag::kPowerRunRecordPeriodMs
                                                              : kRecordPeriodMs;
}

uint32_t PowerService::duty_period_ms() const {
    return context_.diag.profile() == diag::Profile::PowerRun ? diag::kPowerRunRecordPeriodMs
                                                              : kDutyRecordPeriodMs;
}

}  // namespace skyblip::go
