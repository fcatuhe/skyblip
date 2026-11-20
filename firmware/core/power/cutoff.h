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

// INFO: fc 05oct26 a parked reading spreads 35 mV per half hour on E68BD9's run: README.md
constexpr uint16_t kRecoveryMarginMv = 35;

static_assert(kCriticalMv + kRecoveryMarginMv < kLowMv,
              "a cell climbing out of Critical must land in Low, not need to clear Low too");

// INFO: hk 02aug26 an unpopulated divider drifts below this, SoftRF's BATTERY_THRESHOLD_INVALID
// (src/driver/Battery.h:24), and must never power the device off
constexpr uint16_t kImplausibleFloorMv = 1800;

constexpr uint8_t kLevelSamples = 3;

// INFO: fc 30sep26 each value is its diagnostics wire code, and Low joined the ladder last
enum class PowerLevel : uint8_t { Unknown = 0, Normal = 1, Low = 4, Critical = 2, Flat = 3 };

constexpr bool needs_charge(PowerLevel level) {
    return level == PowerLevel::Low || level == PowerLevel::Critical || level == PowerLevel::Flat;
}

enum class DurableWrite : uint8_t {
    Settings = 0,
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

    // INFO: fc 05aug26 stops writes and never sets Flat: too late for a shutdown, README.md
    void on_supply_warning();
    bool supply_warned() const { return supply_warned_; }
    uint32_t supply_warnings() const { return supply_warnings_; }

    bool may_write(DurableWrite kind) const {
        return power::may_write(level_, supply_warned_, kind);
    }

    bool may_refresh(PanelRefresh kind) const {
        return power::may_refresh(level_, supply_warned_, kind);
    }

    uint32_t implausible() const { return implausible_; }

   private:
    static constexpr int kSteps = 3;
    void count(uint16_t divider_mv);
    void forget_runs();
    PowerLevel settled(uint16_t divider_mv) const;

    PowerLevel level_{PowerLevel::Unknown};
    uint8_t below_[kSteps]{};
    uint8_t cleared_[kSteps]{};
    uint32_t implausible_{0};
    uint32_t supply_warnings_{0};
    bool supply_warned_{false};
};

}  // namespace skyblip::power

#endif
