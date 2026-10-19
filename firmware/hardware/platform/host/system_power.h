#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_SYSTEM_POWER_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_SYSTEM_POWER_H

#include "core/power/shutdown.h"
#include "ports/system_power.h"

namespace skyblip::platform::host {

// The rails a test can watch go down, and a cause register it can load with
// whatever the field would have handed us. The order they go down in is the
// same core::power sequence the silicon walks, recorded rather than performed.
class SystemPower : public ports::SystemPower, private power::PowerDownSink {
   public:
    power::ResetCause reset_causes() const override { return causes; }

    void system_off() override {
        performed_count = 0;
        power::power_down(*this);
        system_offs++;
    }

    void reboot() override { reboots++; }

    bool supply_monitor_armed() const override { return supply_monitor_fitted; }

    bool take_supply_warning() override {
        if (!supply_warning) return false;
        supply_warning = false;
        return true;
    }

    power::CellOnGlass cell_on_glass() const override { return retained_cell_on_glass; }
    void set_cell_on_glass(power::CellOnGlass word) override { retained_cell_on_glass = word; }
    bool went_dark_flat() const override { return retained_went_dark_flat; }
    void set_went_dark_flat(bool flat) override { retained_went_dark_flat = flat; }

    // Where a step ended up in what actually ran, -1 if it never did.
    int order_of(power::PowerDownStep step) const {
        for (int i = 0; i < performed_count; i++)
            if (performed_steps[i] == step) return i;
        return -1;
    }

    power::ResetCause causes{power::ResetCause::PowerOn};
    // A host board carries the comparator, because the policy above it is the
    // thing worth testing and a platform that could never warn would leave it
    // unreachable. Settable, so the absent case is a case too.
    bool supply_monitor_fitted{true};
    // What a collapsing rail does to this platform: a case sets it, the product
    // polls it exactly as it polls the silicon.
    bool supply_warning{false};
    // INFO: fc 21sep26 both survive a rig's system_off the way GPREGRET2 survives SYSTEM OFF
    power::CellOnGlass retained_cell_on_glass{power::CellOnGlass::None};
    bool retained_went_dark_flat{false};
    int system_offs{0};
    int reboots{0};
    power::PowerDownStep performed_steps[power::kPowerDownStepCount]{};
    int performed_count{0};

   private:
    void perform(power::PowerDownStep step) override {
        if (performed_count < power::kPowerDownStepCount) performed_steps[performed_count++] = step;
    }
};

}  // namespace skyblip::platform::host

#endif
