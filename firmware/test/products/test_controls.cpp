// What a contact means on a skyBlip Go: the pad moves, and the button acts and authorises.
#include "core/power/shutdown.h"
#include "doctest/doctest.h"
#include "products/common/prompt_answer.h"
#include "products/skyblip_go/input/controls.h"

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

TEST_CASE("controls: a tap pages on the release, not on the contact" *
          doctest::test_suite("main")) {
    go::Controls controls;
    CHECK(controls.read(down(events::Contact::Pad, 1000)) == go::Gesture::None);
    CHECK(controls.tick(1100) == go::Gesture::None);
    CHECK(controls.read(up(events::Contact::Pad, 1200)) == go::Gesture::Tap);

    // One touch, one page: the pad up says nothing more.
    CHECK(controls.tick(5000) == go::Gesture::None);
}

TEST_CASE("controls: the long touch fires while the finger is still on the pad" *
          doctest::test_suite("main")) {
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

TEST_CASE("prompt answer: one press accepts once armed, and only once" *
          doctest::test_suite("main")) {
    common::PromptAnswer answer;
    answer.ask(1000);
    answer.arm_once_quiet(/*readable=*/true, 1000 + common::PromptAnswer::kAnswerSettleMs);
    REQUIRE(answer.armed());
    answer.contact(down(events::Contact::Button, 1700), 1700);
    answer.contact(up(events::Contact::Button, 1800), 1800);
    CHECK(answer.press());
    CHECK_FALSE(answer.armed());
    CHECK_FALSE(answer.press());
}

// A pilot cycling pages or stepping a value produces exactly this, and it must not authorise.
TEST_CASE("prompt answer: an unread prompt, a press already made, or a busy button arms nothing") {
    constexpr uint32_t kSettleMs = common::PromptAnswer::kAnswerSettleMs;

    common::PromptAnswer unread;
    unread.ask(0);
    unread.arm_once_quiet(/*readable=*/false, 5000);
    CHECK_FALSE(unread.press());

    common::PromptAnswer early;
    early.ask(0);
    early.arm_once_quiet(true, kSettleMs - 1);
    CHECK_FALSE(early.press());

    // The down began before the arm and was published after it: it belongs to the pages.
    common::PromptAnswer held;
    held.ask(0);
    held.arm_once_quiet(true, kSettleMs);
    REQUIRE(held.armed());
    held.contact(down(events::Contact::Button, kSettleMs - 10), kSettleMs + 20);
    CHECK_FALSE(held.press());

    common::PromptAnswer busy;
    busy.ask(0);
    busy.contact(down(events::Contact::Button, 100), 100);
    busy.contact(up(events::Contact::Button, 200), 200);
    busy.arm_once_quiet(true, 200 + kSettleMs - 1);
    CHECK_FALSE(busy.armed());
    busy.arm_once_quiet(true, 200 + kSettleMs);
    CHECK(busy.armed());
}

// Three meanings on one button stay disjoint, or a pilot has two of them at once.
TEST_CASE("prompt answer: the settle sits between a press and the power-off hold") {
    CHECK(common::PromptAnswer::kAnswerSettleMs < power::kLongPressHoldMs);

    go::Controls controls;
    controls.read(down(events::Contact::Button, 0));
    CHECK(controls.read(up(events::Contact::Button, power::kLongPressHoldMs + 200)) ==
          go::Gesture::None);
}
