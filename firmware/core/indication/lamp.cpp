#include "core/indication/lamp.h"

namespace skyblip::indication {

namespace {

bool lit_at(const Indication& indication, uint32_t into_cycle_ms) {
    const uint32_t stride = static_cast<uint32_t>(indication.on_ms) + indication.between_ms;
    for (uint32_t flash = 0; flash < indication.flashes; flash++) {
        const uint32_t starts_ms = flash * stride;
        if (into_cycle_ms >= starts_ms && into_cycle_ms < starts_ms + indication.on_ms) return true;
    }
    return false;
}

}  // namespace

const Indication& indication_for(Condition condition) {
    for (int i = 0; i < kRowCount; i++)
        if (kTable[i].condition == condition) return kTable[i].indication;
    return kTable[0].indication;
}

const char* meaning_of(Condition condition) {
    for (int i = 0; i < kRowCount; i++)
        if (kTable[i].condition == condition) return kTable[i].meaning;
    return kTable[0].meaning;
}

const char* to_string(Condition condition) {
    switch (condition) {
        case Condition::Off: return "off";
        case Condition::Alarm: return "alarm";
        case Condition::Critical: return "critical";
        case Condition::Low: return "low";
        case Condition::NoFix: return "no-fix";
        case Condition::Alive: return "alive";
        case Condition::Prompt: return "prompt";
        case Condition::Pairing: return "pairing";
        case Condition::NoFixLinked: return "no-fix-linked";
        case Condition::AliveLinked: return "alive-linked";
        case Condition::NoFixCapturing: return "no-fix-capturing";
        case Condition::AliveCapturing: return "alive-capturing";
        default: return "?";
    }
}

const char* to_string(indication::Lamp lamp) {
    switch (lamp) {
        case indication::Lamp::Green: return "green";
        case indication::Lamp::Red: return "red";
        case indication::Lamp::Blue: return "blue";
        case indication::Lamp::None:
        default: return "dark";
    }
}

Condition condition_for(const Situation& situation) {
    // Read down the priority order in kTable. Nothing below a matching line is
    // consulted, which is what makes the order the whole of the rule.
    if (!situation.running) return Condition::Off;
    if (situation.alarm_level >= kAlarmTakesLamp) return Condition::Alarm;
    if (situation.prompting) return Condition::Prompt;
    if (situation.pairing) return Condition::Pairing;
    switch (situation.power_level) {
        case power::PowerLevel::Critical:
        case power::PowerLevel::Flat: return Condition::Critical;
        case power::PowerLevel::Low: return Condition::Low;
        case power::PowerLevel::Unknown:
        case power::PowerLevel::Normal: break;
    }
    if (situation.capturing)
        return situation.fix_valid ? Condition::AliveCapturing : Condition::NoFixCapturing;
    if (situation.linked)
        return situation.fix_valid ? Condition::AliveLinked : Condition::NoFixLinked;
    return situation.fix_valid ? Condition::Alive : Condition::NoFix;
}

Command Policy::update(const Situation& situation, uint32_t now_ms,
                       const timing::ClockState& clock) {
    const Condition wanted = condition_for(situation);
    if (wanted != condition_ || !started_) {
        condition_ = wanted;
        started_ = true;
        // A condition that has just changed shows itself immediately: the flash a
        // pilot is owed is the one that follows the event, not the one that
        // follows the rest of the previous cycle.
        cycle_start_ms_ = now_ms;
        on_grid_ = false;
    }

    const Indication& indication = indication_for(condition_);
    const bool lit = indication.lamp != indication::Lamp::None && indication.on_ms != 0 &&
                     lit_at(indication, into_cycle_ms(indication, now_ms, clock));

    Command command{};
    command.lamp = lit ? indication.lamp : indication::Lamp::None;
    command.changed = command.lamp != shown_;
    shown_ = command.lamp;
    return command;
}

// INFO: fc 12nov26 the grid is joined only where both phases are dark, so no flash is cut short
uint32_t Policy::into_cycle_ms(const Indication& indication, uint32_t now_ms,
                               const timing::ClockState& clock) {
    const uint32_t cycle = cycle_ms(indication);
    if (cycle == 0) return 0;
    uint32_t into = (now_ms - cycle_start_ms_) % cycle;
    const std::optional<uint64_t> utc_ms = timing::utc_ms(clock);
    if (!utc_ms) {
        on_grid_ = false;
    } else {
        const uint32_t on_grid = static_cast<uint32_t>(*utc_ms % kUtcGridMs) % cycle;
        if (!on_grid_ && !lit_at(indication, into) && !lit_at(indication, on_grid)) on_grid_ = true;
        if (on_grid_) into = on_grid;
    }
    cycle_start_ms_ = now_ms - into;
    return into;
}

}  // namespace skyblip::indication
