#include "products/skyblip_bravo/services/button.h"

#include "core/events/input.h"
#include "core/indication/lamp.h"
#include "core/power/shutdown.h"

namespace skyblip::bravo {

namespace {
bool settled_for_a_double_press(uint32_t now_ms, uint32_t since_ms) {
    return now_ms - since_ms >= common::ConfirmGesture::kDoublePressWindowMs;
}
}  // namespace

void ButtonService::tick(uint32_t now_ms) {
    sync_prompt(now_ms);
    sync_receiving(now_ms);
    events::ContactEvent event{};
    while (context_.bus.input.pop(event)) {
        if (event.contact != events::Contact::Button) continue;
        // INFO: fc 12nov26 a hold past kLongPressHoldMs is the shutdown sequencer's, never a press
        const bool pressed = !event.down && answer_.pressing() &&
                             event.at_ms - answer_.pressed_ms() < power::kLongPressHoldMs;
        answer_.contact(event, now_ms);
        if (pressed) press(now_ms);
    }
    if (answering()) {
        answer_.arm_once_quiet(prompt_readable(), now_ms);
        resolve(answer_.tick(now_ms));
    }
}

void ButtonService::sync_prompt(uint32_t now_ms) {
    const comms::Pending pending = config_.prompt_allowed_in(context_.state.flight.confirmed_state)
                                       ? config_.pending()
                                       : comms::Pending::None;
    const uint32_t staged = config_.prompts_staged();
    if (pending == prompt_ && staged == prompts_staged_) return;
    prompt_ = pending;
    prompts_staged_ = staged;
    answer_.ask(now_ms);
}

// INFO: fc 12nov26 as on the Go's glass: the lamp shows the prompt and the thumb is up and still
bool ButtonService::prompt_readable() const {
    if (!ports::has(context_.roles.capabilities, ports::Capability::Indicator)) return true;
    return alarm_.indicator_condition() == indication::Condition::Prompt;
}

void ButtonService::sync_receiving(uint32_t now_ms) {
    const bool receiving = config_.receiving_firmware();
    if (receiving == receiving_) return;
    receiving_ = receiving;
    receiving_since_ms_ = now_ms;
}

void ButtonService::press(uint32_t now_ms) {
    if (answer_.armed()) {
        resolve(answer_.press(now_ms));
        return;
    }
    if (alarm_stands()) {
        alarm_.dismiss();
        return;
    }
    if (answering()) return;
    // INFO: fc 12nov26 the press that confirmed the upload must not be the one that cancels it
    if (receiving_ && settled_for_a_double_press(now_ms, receiving_since_ms_)) config_.cancel();
}

void ButtonService::resolve(common::Answer answer) {
    if (answer == common::Answer::None) return;
    if (answer == common::Answer::Confirm)
        config_.confirm();
    else
        config_.cancel();
    prompt_ = comms::Pending::None;
    answer_.disarm();
}

}  // namespace skyblip::bravo
