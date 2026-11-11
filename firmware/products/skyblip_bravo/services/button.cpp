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
    pairing_.sync(peer_.requested(), pairing_readable(), now_ms);
    sync_receiving(now_ms);
    events::ContactEvent event{};
    while (context_.bus.input.pop(event)) {
        if (event.contact != events::Contact::Button) continue;
        if (event.down) {
            pressing_ = true;
            pressed_ms_ = event.at_ms;
            // INFO: fc 12nov26 a contact begun before the gesture armed is never spent on it
            pressed_while_armed_ = confirm_.armed() || pairing_.armed();
            continue;
        }
        // INFO: fc 12nov26 a hold past kLongPressHoldMs is the shutdown sequencer's, never a press
        const bool pressed = pressing_ && event.at_ms - pressed_ms_ < power::kLongPressHoldMs;
        pressing_ = false;
        released_ms_ = now_ms;
        released_once_ = true;
        if (pressed) press(now_ms);
    }
    arm_once_quiet(now_ms);
    if (!pressing_)
        pairing_.arm_once_quiet(pairing_readable(), released_once_, released_ms_, now_ms);
    if (answering()) resolve(confirm_.tick(now_ms));
}

void ButtonService::sync_prompt(uint32_t now_ms) {
    const comms::Pending pending = config_.prompt_allowed_in(context_.state.flight.confirmed_state)
                                       ? config_.pending()
                                       : comms::Pending::None;
    if (pending == prompt_) return;
    prompt_ = pending;
    prompt_since_ms_ = now_ms;
    confirm_.disarm();
}

// INFO: fc 12nov26 as on the Go's glass: the lamp shows the prompt and the thumb is up and still
void ButtonService::arm_once_quiet(uint32_t now_ms) {
    if (!answering() || confirm_.armed() || pressing_) return;
    const bool quiet = settled_for_a_double_press(now_ms, prompt_since_ms_) &&
                       (!released_once_ || settled_for_a_double_press(now_ms, released_ms_));
    if (prompt_readable() && quiet) confirm_.arm(now_ms);
}

bool ButtonService::pairing_readable() const {
    if (!ports::has(context_.roles.capabilities, ports::Capability::Indicator)) return true;
    return alarm_.indicator_condition() == indication::Condition::Pairing;
}

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
    if (confirm_.armed()) {
        if (pressed_while_armed_) resolve(confirm_.press(now_ms));
        return;
    }
    if (alarm_stands()) {
        alarm_.dismiss();
        return;
    }
    if (pairing_.armed()) {
        if (pressed_while_armed_ && peer_.answer(now_ms)) pairing_.spend();
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
    confirm_.disarm();
}

}  // namespace skyblip::bravo
