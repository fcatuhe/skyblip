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

uint32_t until_edge_ms(const Indication& indication, uint32_t into_cycle_ms) {
    const uint32_t stride = static_cast<uint32_t>(indication.on_ms) + indication.between_ms;
    uint32_t edge_ms = cycle_ms(indication);
    for (uint32_t flash = indication.flashes; flash-- > 0;) {
        const uint32_t starts_ms = flash * stride;
        if (starts_ms + indication.on_ms > into_cycle_ms) edge_ms = starts_ms + indication.on_ms;
        if (starts_ms > into_cycle_ms) edge_ms = starts_ms;
    }
    return edge_ms - into_cycle_ms;
}

const Indication& indication_for(Condition condition) {
    for (int i = 0; i < kRowCount; i++)
        if (kTable[i].condition == condition) return kTable[i].indication;
    return kTable[0].indication;
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

const char* to_string(Colour colour) {
    switch (colour) {
        case Colour::Green: return "green";
        case Colour::Red: return "red";
        case Colour::Blue: return "blue";
        case Colour::None:
        default: return "dark";
    }
}

Condition condition_for(const Situation& situation) {
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
        cycle_start_ms_ = now_ms;
        on_grid_ = false;
    }

    const Indication& indication = indication_for(condition_);
    into_ms_ = into_cycle_ms(indication, now_ms, clock);
    updated_ms_ = now_ms;
    const bool lit =
        indication.lamp != Colour::None && indication.on_ms != 0 && lit_at(indication, into_ms_);

    Command command{};
    command.lamp = lit ? indication.lamp : Colour::None;
    command.changed = command.lamp != shown_;
    shown_ = command.lamp;
    return command;
}

uint32_t Policy::until_change_ms(uint32_t now_ms) const {
    const Indication& indication = indication_for(condition_);
    const uint32_t cycle = cycle_ms(indication);
    if (indication.lamp == Colour::None || indication.on_ms == 0 || cycle == 0) return UINT32_MAX;
    return until_edge_ms(indication, (into_ms_ + (now_ms - updated_ms_)) % cycle);
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
