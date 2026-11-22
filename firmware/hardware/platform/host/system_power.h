#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_SYSTEM_POWER_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_SYSTEM_POWER_H

#include "core/power/reset_reason.h"
#include "core/power/shutdown.h"
#include "core/power/wake.h"

namespace skyblip::platform::host {

class SystemPower : private power::PowerDownSink {
   public:
    power::ResetCause reset_causes() const { return causes; }

    void system_off() {
        performed_count = 0;
        power::power_down(*this);
        system_offs++;
    }

    bool take_supply_warning() {
        if (!supply_warning) return false;
        supply_warning = false;
        return true;
    }

    power::CellOnGlass cell_on_glass() const { return retained_cell_on_glass; }
    void set_cell_on_glass(power::CellOnGlass word) { retained_cell_on_glass = word; }
    bool went_dark_flat() const { return retained_went_dark_flat; }
    void set_went_dark_flat(bool flat) { retained_went_dark_flat = flat; }

    int order_of(power::PowerDownStep step) const {
        for (int i = 0; i < performed_count; i++)
            if (performed_steps[i] == step) return i;
        return -1;
    }

    power::ResetCause causes{power::ResetCause::PowerOn};
    bool supply_warning{false};
    // INFO: fc 21sep26 both survive a rig's system_off the way GPREGRET2 survives SYSTEM OFF
    power::CellOnGlass retained_cell_on_glass{power::CellOnGlass::None};
    bool retained_went_dark_flat{false};
    int system_offs{0};
    power::PowerDownStep performed_steps[power::kPowerDownStepCount]{};
    int performed_count{0};

   private:
    void perform(power::PowerDownStep step) override {
        if (performed_count < power::kPowerDownStepCount) performed_steps[performed_count++] = step;
    }
};

}  // namespace skyblip::platform::host

#endif
