#ifndef SKYBLIP_PORTS_SYSTEM_POWER_H
#define SKYBLIP_PORTS_SYSTEM_POWER_H

#include "core/power/reset_reason.h"
#include "core/power/wake.h"

namespace skyblip::ports {

class SystemPower {
   public:
    virtual ~SystemPower() = default;

    // INFO: fc 20nov26 read and cleared at boot, since a latching register repeats it until cleared
    virtual power::ResetCause reset_causes() const { return power::ResetCause::None; }

    virtual void system_off() {}

    // INFO: fc 20nov26 false with no comparator, so core/power's may_write keeps its voltage half
    virtual bool supply_monitor_armed() const { return false; }

    virtual bool take_supply_warning() { return false; }

    // INFO: fc 20nov26 these bits outlive the rails, and a platform without storage answers None
    virtual power::CellOnGlass cell_on_glass() const { return power::CellOnGlass::None; }
    virtual void set_cell_on_glass(power::CellOnGlass word) { (void)word; }
    virtual bool went_dark_flat() const { return false; }
    virtual void set_went_dark_flat(bool flat) { (void)flat; }
};

}  // namespace skyblip::ports

#endif
