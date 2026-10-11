// core/power/battery.h: what a cell's terminal voltage means, on charge and off.
#ifndef SKYBLIP_CORE_POWER_BATTERY_H
#define SKYBLIP_CORE_POWER_BATTERY_H

#include <cstdint>

#include "core/events/sensor.h"
#include "core/power/cutoff.h"

namespace skyblip::power {

constexpr uint16_t kEmptyMv = kFlatMv;
constexpr uint16_t kFullMv = 4200;
// INFO: fc 05oct26 no 4.2 V cell reads this high, a USB rail does: README.md
constexpr uint16_t kImplausibleCeilingMv = 4700;

static_assert(kFullMv < kImplausibleCeilingMv, "a full cell is a reading the gauge believes");

constexpr bool plausible_mv(uint16_t millivolts) {
    return millivolts > kImplausibleFloorMv && millivolts <= kImplausibleCeilingMv;
}

// INFO: fc 03aug26 The per-unit trim, in millivolts, added to a reading before
// anything reads meaning into it. The divider ratio is a devicetree fact and
// Zephyr's voltage-divider driver owns the conversion, which is right; what it
// cannot know is this unit. Two 1% resistors put the ratio out by up to 2%,
// which at a full cell is 84 mV, and the SAADC's own gain and reference error
// adds to that - tens of millivolts, on a curve whose flat middle spends 250 mV
// crossing 60 percentage points. So one number, measured once on the line
// against a bench supply, and every later battery complaint is answerable.
//
// The bound is three times the divider's own worst case. Past it the reading is
// not out of trim, it is wired wrong or reading another rail, and a calibration
// field that accepts anything is a support incident of its own. It is enforced
// where the value enters the device (core/settings validates it, on the blob and
// on the JSON alike); nothing here re-checks it, only the arithmetic is kept
// from leaving the range a millivolt reading can hold.
constexpr int16_t kCalibrationLimitMv = 250;

// The sample as this unit's front end should have read it. Applied once, by the
// service that drains the queue, so the gauge and the cutoff monitor act on the
// same millivolts - a cutoff that fired 40 mV early on a trimmed unit would be
// the calibration causing the failure it exists to prevent.
uint16_t calibrated_mv(uint16_t raw_mv, int16_t offset_mv);
events::BatterySample calibrated(const events::BatterySample& raw, int16_t offset_mv);

// INFO: fc 05oct26 off the cable only: on it the divider reads the USB rail, README.md
uint8_t percent_from_mv(uint16_t millivolts);

struct BatteryState {
    uint16_t millivolts{0};
    uint16_t sample_mv{0};
    uint8_t percent{0};
    bool external_power{false};
    // INFO: fc 05oct26 the cable is in: no pin says when the charger is done, README.md
    bool charging{false};
    bool valid{false};
};

// Readings become something a pilot can read. A 14 dBm burst sags the rail for
// exactly as long as it lasts, so the gauge takes the median of the last three
// readings rather than an average: a transient is discarded whole, while a cell
// that is really moving is followed within two samples. Off the cable the
// percentage only falls. On the cable, or on a reading outside plausible_mv, the
// gauge has no cell to show: valid drops, millivolts is what the divider read,
// and the next reading of the cell starts the median over.
//
// INFO: fc 03aug26 No time-remaining estimate here, and that is a decision, not
// an omission. OGN derives a drift rate from a 32-deep delay line on the voltage
// (nrf52-ogn-tracker src/proc.cpp:326-345) and a "40 minutes" reading is worth
// more to a pilot than "38%", so the reason has to be better than cost.
//
// It is this: minutes-remaining is percent divided by the SLOPE of the curve
// above, and that curve is the textbook shape for a generic Li-ion cell, not
// this pack measured on this board (the note on kDischargeCurve says so). An
// error of a few percent in the level is a few percent; the same error in the
// slope is a factor. Nothing else here is a guess about the future - the
// median-of-three, the curve and the direction rule all describe a reading
// that has already happened - and a percentage is a statement of state a pilot
// discounts for himself, while a duration is a promise he plans a leg on. Two
// facts make the promise unbackable today: the fitted cell has no identified
// manufacturer or datasheet (project/research/enclosure-and-mount-go.md:59), and
// there is no measured power budget or runtime figure for any device state at
// all - not receive, not transmit, not sleep, not this panel. A figure
// a pilot reads has to trace to project/research/, and this one would trace to
// nothing.
//
// What unblocks it, and it is one bench day, not a quarter: a logged discharge
// of a fitted unit under the real dwell map, which the power budget needs
// anyway. That
// log fixes the curve, and with a real curve percent falls linearly in time
// under a constant load, so the estimator is small and needs no new sampling -
// a fixed-depth ring of (percent, millisecond) pairs recorded on each whole
// percent step, rate taken across the ring, published only once the ring spans
// at least three percent, cleared whenever external power appears. Written down
// here so the next person implements the measured version rather than
// rediscovering why the unmeasured one was refused.
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
