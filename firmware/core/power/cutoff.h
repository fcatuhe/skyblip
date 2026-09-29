// core/power/cutoff.h: when a falling cell stops being a reading and becomes an
// action. The read side (is this millivolt figure believable at all) lives in the
// battery adapter; this is the acting half.
#ifndef SKYBLIP_CORE_POWER_CUTOFF_H
#define SKYBLIP_CORE_POWER_CUTOFF_H

#include <cstdint>

#include "core/events/sensor.h"

namespace skyblip::power {

// INFO: fc 20sep26 the ladder a 4.2 V LiPo pouch is read on, step by step: README.md
constexpr uint16_t kLowMv = 3600;
constexpr uint16_t kCriticalMv = 3500;
constexpr uint16_t kFlatMv = 3200;

static_assert(kFlatMv < kCriticalMv && kCriticalMv < kLowMv,
              "the ladder is read downwards: a step out of order is a step nobody reaches");

// INFO: hk 02aug26 an unpopulated or unconnected divider reads as a slow drift
// near zero, not as a flat cell. SoftRF calls the same floor
// BATTERY_THRESHOLD_INVALID (src/driver/Battery.h:24) and refuses to act below
// it, because a floating ADC that can power the device off is worse than no
// gauge at all.
constexpr uint16_t kImplausibleFloorMv = 1800;

// More than two consecutive samples, so the third one acts. A transmit burst
// sags the rail for as long as it lasts, and the sampler is slower than a burst,
// but a charger unplugged mid-sample is not.
constexpr uint8_t kLevelSamples = 3;

// INFO: fc 30sep26 each value is its diagnostics wire code, and Low joined the ladder last
enum class PowerLevel : uint8_t { Unknown = 0, Normal = 1, Low = 4, Critical = 2, Flat = 3 };

const char* to_string(PowerLevel level);

constexpr bool needs_charge(PowerLevel level) {
    return level == PowerLevel::Low || level == PowerLevel::Critical || level == PowerLevel::Flat;
}

// What a durable write is for. Only two kinds exist and the difference between
// them is the whole rule below: one is a convenience a pilot can make again, the
// other is the evidence of the flight that is ending.
enum class DurableWrite : uint8_t {
    // The settings blob, on the internal NVS sector. Rewritten on every accepted
    // change, and the sector NVS garbage-collects is the same internal flash the
    // running image executes from.
    Settings = 0,
    // The flight log record. Losing it loses the flight, and the moment it is
    // most needed is the moment the cell is going: a landing out, a pack that
    // sagged under a burst, a device switched off in a hurry.
    FlightRecord = 1,
};

// INFO: fc 30sep26 Critical, Flat or a supply warning write only the flight record: README.md
bool may_write(PowerLevel level, bool supply_warned, DurableWrite kind);

enum class PanelRefresh : uint8_t { Routine, Park };

// INFO: fc 06sep26 the panel makes its own drive rails: a lost supply latches neither frame
bool may_refresh(PowerLevel level, bool supply_warned, PanelRefresh kind);

// INFO: fc 30sep26 latches at Flat: a cell relaxing once the radio is quiet cannot cancel it
class CutoffMonitor {
   public:
    PowerLevel apply(const events::BatterySample& sample);

    PowerLevel level() const { return level_; }
    bool cutoff() const { return level_ == PowerLevel::Flat; }

    // The nRF52840's power-failure comparator fired (POFCON, POFWARN). Latching:
    // the rail crossed a threshold it has no business crossing, and a rail that
    // came back up does not make that un-happen.
    //
    // INFO: fc 05aug26 stops writes and never sets Flat: too late for a shutdown, README.md
    void on_supply_warning();
    bool supply_warned() const { return supply_warned_; }
    // How many times it fired. A unit that reports any at all on a healthy cell
    // has a supply problem, or a threshold set too high, and both are bench work.
    uint32_t supply_warnings() const { return supply_warnings_; }

    // The rule above, asked of what this monitor currently knows.
    bool may_write(DurableWrite kind) const {
        return power::may_write(level_, supply_warned_, kind);
    }

    bool may_refresh(PanelRefresh kind) const {
        return power::may_refresh(level_, supply_warned_, kind);
    }

    // Readings the sanity floor threw away. A gauge that never rises above zero
    // here is a gauge nobody should trust.
    uint32_t implausible() const { return implausible_; }

   private:
    static constexpr int kSteps = 3;
    void count(uint16_t millivolts);
    void forget_runs();
    PowerLevel settled() const;

    PowerLevel level_{PowerLevel::Unknown};
    uint8_t below_[kSteps]{};
    uint8_t at_or_above_[kSteps]{};
    uint32_t implausible_{0};
    uint32_t supply_warnings_{0};
    bool supply_warned_{false};
};

}  // namespace skyblip::power

#endif
