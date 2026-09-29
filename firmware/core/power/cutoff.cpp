#include "core/power/cutoff.h"

#include <iterator>

#include "core/events/sensor.h"

namespace skyblip::power {

namespace {

struct Step {
    PowerLevel level;
    uint16_t entered_below_mv;
};

constexpr Step kLadder[] = {
    {PowerLevel::Low, kLowMv},
    {PowerLevel::Critical, kCriticalMv},
    {PowerLevel::Flat, kFlatMv},
};

int depth(PowerLevel level) {
    for (int i = 0; i < static_cast<int>(std::size(kLadder)); i++)
        if (kLadder[i].level == level) return i + 1;
    return 0;
}

uint8_t lengthened(uint8_t run) {
    return run < kLevelSamples ? static_cast<uint8_t>(run + 1) : run;
}

}  // namespace

const char* to_string(PowerLevel level) {
    switch (level) {
        case PowerLevel::Normal: return "OK";
        case PowerLevel::Low: return "LOW";
        case PowerLevel::Critical: return "CRITICAL";
        case PowerLevel::Flat: return "FLAT";
        case PowerLevel::Unknown: break;
    }
    return "--";
}

// A flight log record is written whatever the level says, and that is the point
// of the rule rather than an exception to it: it is the one write whose value is
// highest exactly when the cell is lowest. It is also the cheap one - an append
// to the external SPI NOR log region, not a rewrite of an NVS sector that
// garbage-collects the flash the image runs from.
bool may_write(PowerLevel level, bool supply_warned, DurableWrite kind) {
    if (kind == DurableWrite::FlightRecord) return true;
    if (supply_warned) return false;
    return level != PowerLevel::Critical && level != PowerLevel::Flat;
}

bool may_refresh(PowerLevel level, bool supply_warned, PanelRefresh kind) {
    if (supply_warned) return false;
    if (kind == PanelRefresh::Park) return true;
    return level != PowerLevel::Flat;
}

void CutoffMonitor::on_supply_warning() {
    supply_warnings_++;
    supply_warned_ = true;
}

PowerLevel CutoffMonitor::apply(const events::BatterySample& sample) {
    if (level_ == PowerLevel::Flat) return level_;

    if (sample.millivolts <= kImplausibleFloorMv) {
        implausible_++;
        forget_runs();
        return level_;
    }

    // INFO: fc 30sep26 the charge current holds the terminal above the cell, nothing may act on it
    if (sample.external_power) {
        forget_runs();
        level_ = PowerLevel::Normal;
        return level_;
    }

    count(sample.millivolts);
    level_ = settled();
    return level_;
}

void CutoffMonitor::count(uint16_t millivolts) {
    static_assert(std::size(kLadder) == kSteps,
                  "a step the monitor cannot count is a step nobody reaches");
    for (int i = 0; i < kSteps; i++) {
        const bool below = millivolts < kLadder[i].entered_below_mv;
        below_[i] = below ? lengthened(below_[i]) : 0;
        at_or_above_[i] = below ? 0 : lengthened(at_or_above_[i]);
    }
}

void CutoffMonitor::forget_runs() {
    for (int i = 0; i < kSteps; i++) {
        below_[i] = 0;
        at_or_above_[i] = 0;
    }
}

PowerLevel CutoffMonitor::settled() const {
    const int current = depth(level_);
    for (int i = kSteps - 1; i >= current; i--)
        if (below_[i] >= kLevelSamples) return kLadder[i].level;

    if (level_ == PowerLevel::Unknown)
        return at_or_above_[0] > 0 ? PowerLevel::Normal : PowerLevel::Unknown;

    int risen = current;
    while (risen > 0 && at_or_above_[risen - 1] >= kLevelSamples) risen--;
    return risen == 0 ? PowerLevel::Normal : kLadder[risen - 1].level;
}

}  // namespace skyblip::power
