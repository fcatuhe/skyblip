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

    void system_off(power::ButtonWake button_wake) override {
        performed = 0;
        power::power_down(*this, button_wake);
        offs++;
    }

    void reboot() override { reboots++; }

    bool supply_monitor_armed() const override { return supply_monitor; }

    bool take_supply_warning() override {
        if (!supply_warning) return false;
        supply_warning = false;
        return true;
    }

    bool flat_on_glass() const override { return flat_glass; }
    void set_flat_on_glass(bool flat) override { flat_glass = flat; }
    bool went_dark_flat() const override { return dark_flat; }
    void set_went_dark_flat(bool flat) override { dark_flat = flat; }

    // Where a step ended up in what actually ran, -1 if it never did.
    int order_of(power::PowerDownStep step) const {
        for (int i = 0; i < performed; i++)
            if (sequence[i] == step) return i;
        return -1;
    }

    power::ResetCause causes{power::ResetCause::PowerOn};
    // A host board carries the comparator, because the policy above it is the
    // thing worth testing and a platform that could never warn would leave it
    // unreachable. Settable, so the absent case is a case too.
    bool supply_monitor{true};
    // What a collapsing rail does to this platform: a case sets it, the product
    // polls it exactly as it polls the silicon.
    bool supply_warning{false};
    // INFO: fc 21sep26 both survive a rig's system_off the way GPREGRET2 survives SYSTEM OFF
    bool flat_glass{false};
    bool dark_flat{false};
    int offs{0};
    int reboots{0};
    power::PowerDownStep sequence[power::kPowerDownStepCount]{};
    int performed{0};

   private:
    void perform(power::PowerDownStep step) override {
        if (performed < power::kPowerDownStepCount) sequence[performed++] = step;
    }
};

}  // namespace skyblip::platform::host

#endif
