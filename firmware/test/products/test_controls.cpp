// What a contact means on a skyBlip Go: the pad moves, the button acts, and the double press
// authorises.
#include "core/power/shutdown.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/input/controls.h"
#include "products/skyblip_go/input/gesture.h"

using namespace skyblip;

namespace {

constexpr uint32_t kLongTouchHoldMs = go::Controls::kLongTouchHoldMs;

events::ContactEvent down(events::Contact contact, uint32_t at_ms) {
    return events::ContactEvent{contact, true, at_ms};
}

events::ContactEvent up(events::Contact contact, uint32_t at_ms) {
    return events::ContactEvent{contact, false, at_ms};
}

go::Gesture touch(go::Controls& controls, uint32_t at_ms, uint32_t held_ms) {
    controls.read(down(events::Contact::Pad, at_ms));
    for (uint32_t elapsed = 0; elapsed <= held_ms; elapsed += 10)
        if (const go::Gesture held = controls.tick(at_ms + elapsed); held != go::Gesture::None)
            return held;
    return controls.read(up(events::Contact::Pad, at_ms + held_ms));
}

}  // namespace

TEST_CASE("controls: a tap pages on the release, not on the contact") {
    go::Controls controls;
    CHECK(controls.read(down(events::Contact::Pad, 1000)) == go::Gesture::None);
    CHECK(controls.tick(1100) == go::Gesture::None);
    CHECK(controls.read(up(events::Contact::Pad, 1200)) == go::Gesture::Tap);

    // One touch, one page: the pad up says nothing more.
    CHECK(controls.tick(5000) == go::Gesture::None);
}

TEST_CASE("controls: the long touch fires while the finger is still on the pad") {
    go::Controls controls;
    controls.read(down(events::Contact::Pad, 1000));
    CHECK(controls.tick(1000 + kLongTouchHoldMs - 1) == go::Gesture::None);
    CHECK(controls.tick(1000 + kLongTouchHoldMs) == go::Gesture::LongTouch);

    // Held on, and then let go of: neither fires again, and the release is not a page.
    CHECK(controls.tick(1000 + kLongTouchHoldMs + 3000) == go::Gesture::None);
    CHECK(controls.read(up(events::Contact::Pad, 5000)) == go::Gesture::None);
}

// A service that polled late read the pad up, called it a page, and stole the way home.
TEST_CASE("controls: a release nobody ticked through is still the long touch it was") {
    go::Controls controls;
    controls.read(down(events::Contact::Pad, 1000));
    CHECK(controls.read(up(events::Contact::Pad, 1000 + kLongTouchHoldMs + 500)) ==
          go::Gesture::LongTouch);
}

TEST_CASE("controls: a press lands when the thumb comes off, not when it arrives") {
    go::Controls controls;
    CHECK(controls.read(down(events::Contact::Button, 1000)) == go::Gesture::None);
    CHECK(controls.read(up(events::Contact::Button, 1200)) == go::Gesture::Press);
}

// A flashed unit paged under the thumb, two seconds before the rails went.
TEST_CASE("controls: a press held into the power-off hold says nothing, then or on release") {
    go::Controls controls;
    controls.read(down(events::Contact::Button, 0));
    CHECK(controls.read(up(events::Contact::Button, power::kLongPressHoldMs)) == go::Gesture::None);

    go::Controls quick;
    quick.read(down(events::Contact::Button, 0));
    CHECK(quick.read(up(events::Contact::Button, power::kLongPressHoldMs - 1)) ==
          go::Gesture::Press);
}

// A touch with the button down is the stow (core/power/shutdown.h), never a page.
TEST_CASE("controls: a touch the button joins says nothing, in either direction") {
    go::Controls controls;
    controls.read(down(events::Contact::Button, 500));
    CHECK(touch(controls, 1000, kLongTouchHoldMs + 1000) == go::Gesture::None);

    go::Controls joined;
    joined.read(down(events::Contact::Pad, 1000));
    CHECK(joined.read(down(events::Contact::Button, 1000 + kLongTouchHoldMs - 200)) ==
          go::Gesture::None);
    CHECK(joined.tick(1000 + kLongTouchHoldMs + 500) == go::Gesture::None);
    CHECK(joined.read(up(events::Contact::Button, 1000 + kLongTouchHoldMs + 600)) ==
          go::Gesture::Press);

    // Releasing the button does not rescue that touch: the pad has to be let go of first.
    CHECK(joined.read(up(events::Contact::Pad, 1000 + kLongTouchHoldMs + 900)) ==
          go::Gesture::None);
    CHECK(touch(joined, 9000, kLongTouchHoldMs + 100) == go::Gesture::LongTouch);
}

TEST_CASE("gesture: two presses inside the window authorise, one press refuses") {
    go::ConfirmGesture g;
    g.arm(1000);
    CHECK(g.press(1100) == go::Answer::None);  // one press decides nothing yet
    CHECK(g.press(1250) == go::Answer::Confirm);
    CHECK_FALSE(g.armed());  // spent: the same pair cannot confirm twice
    CHECK(g.press(1300) == go::Answer::None);

    // The page press, made at a prompt. It is not ignored - it refuses.
    go::ConfirmGesture lone;
    lone.arm(1000);
    CHECK(lone.press(1100) == go::Answer::None);
    CHECK(lone.tick(1100 + go::ConfirmGesture::kDoublePressWindowMs - 1) == go::Answer::None);
    CHECK(lone.tick(1100 + go::ConfirmGesture::kDoublePressWindowMs) == go::Answer::Cancel);
    CHECK_FALSE(lone.armed());
}

// A pilot cycling pages produces exactly this, and it must not add up to a firmware upload.
TEST_CASE("gesture: presses made before the prompt, and slow presses, authorise nothing") {
    go::ConfirmGesture g;
    CHECK(g.press(500) == go::Answer::None);  // disarmed: paging, not answering
    CHECK(g.press(600) == go::Answer::None);
    CHECK(g.tick(2000) == go::Answer::None);

    // The prompt arrives between the two halves of a page double-tap. The half
    // that came first belongs to the pages, and cannot be counted here.
    g.arm(650);
    CHECK(g.press(700) == go::Answer::None);
    CHECK(g.tick(700 + go::ConfirmGesture::kDoublePressWindowMs) == go::Answer::Cancel);

    go::ConfirmGesture slow;
    slow.arm(0);
    CHECK(slow.press(100) == go::Answer::None);
    CHECK(slow.press(100 + go::ConfirmGesture::kDoublePressWindowMs + 1) == go::Answer::None);
}

// Three meanings on one button stay disjoint, or a pilot has two of them at once.
TEST_CASE("gesture: the authorising window sits between a press and the power-off hold") {
    CHECK(go::ConfirmGesture::kDoublePressWindowMs < power::kLongPressHoldMs);

    go::Controls controls;
    go::ConfirmGesture g;
    g.arm(0);
    controls.read(down(events::Contact::Button, 0));
    CHECK(controls.read(up(events::Contact::Button, power::kLongPressHoldMs + 200)) ==
          go::Gesture::None);
    CHECK(g.tick(power::kLongPressHoldMs) == go::Answer::None);
    CHECK(g.armed());
}
