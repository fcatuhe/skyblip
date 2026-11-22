#ifndef SKYBLIP_CORE_POWER_BATTERY_H
#define SKYBLIP_CORE_POWER_BATTERY_H

#include <cstdint>

#include "core/events/sensor.h"
#include "core/power/cutoff.h"

namespace skyblip::power {

// INFO: fc 20nov26 the gauge needs three readings to reject a transient, so this still shows charge
// on the first screen
constexpr uint32_t kBatteryPeriodMs = 1000;

constexpr uint16_t kEmptyMv = kFlatMv;
constexpr uint16_t kFullMv = 4200;
// INFO: fc 05oct26 no 4.2 V cell reads this high, a USB rail does: README.md
constexpr uint16_t kImplausibleCeilingMv = 4700;

static_assert(kFullMv < kImplausibleCeilingMv, "a full cell is a reading the gauge believes");

constexpr bool plausible_mv(uint16_t divider_mv) {
    return divider_mv > kImplausibleFloorMv && divider_mv <= kImplausibleCeilingMv;
}

// INFO: fc 20nov26 three times the divider's worst case: past it the reading is wired wrong, not
// out of trim, README.md
constexpr int16_t kCalibrationCeilingMv = 250;

uint16_t calibrated_mv(uint16_t raw_mv, int16_t offset_mv);
events::BatterySample calibrated(const events::BatterySample& raw, int16_t offset_mv);

// INFO: fc 05oct26 off the cable only: on it the divider reads the USB rail, README.md
uint8_t percent_from_mv(uint16_t cell_mv);

struct BatteryState {
    uint16_t cell_mv{0};
    uint16_t sample_mv{0};
    uint8_t percent{0};
    bool external_power{false};
    // INFO: fc 05oct26 the cable is in: no pin says when the charger is done, README.md
    bool charging{false};
    bool valid{false};
};

class Gauge {
   public:
    void apply(const events::BatterySample& sample);

    const BatteryState& state() const { return state_; }
    uint32_t refused() const { return refused_; }

   private:
    static constexpr int kWindowSamples = 3;

    uint16_t recent_[kWindowSamples]{};
    int seen_{0};
    uint32_t refused_{0};
    BatteryState state_{};
};

}  // namespace skyblip::power

#endif
