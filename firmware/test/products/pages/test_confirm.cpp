// The authorisation prompt, read back off the glass. With BLE pairing off, this
// page is the only thing standing between a stranger's phone and the secondary
// slot, so "there is ink on the panel" is not the claim: "the panel says POWER
// OFF, and says which press allows it" is.
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "core/comms/config.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/confirm.h"
#include "test/support/frame_glass.h"

using namespace skyblip;
using namespace skyblip::go;

namespace {

ConfirmSnapshot prompt(comms::Pending pending) {
    ConfirmSnapshot snapshot;
    snapshot.title = comms::pending_title(pending);
    snapshot.detail = comms::pending_detail(pending);
    snapshot.seconds_left = comms::kPromptCeilingMs / 1000;
    return snapshot;
}

constexpr comms::Pending kEveryPrompt[] = {comms::Pending::Set,      comms::Pending::Dfu,
                                           comms::Pending::Recovery, comms::Pending::PowerOff,
                                           comms::Pending::EraseLog, comms::Pending::GnssColdStart,
                                           comms::Pending::Register, comms::Pending::Capture};

}  // namespace

TEST_CASE("confirm page: the pilot is told which operation they are authorizing") {
    Glass dfu;
    draw_confirm(dfu, prompt(comms::Pending::Dfu));
    CHECK(reads_heading(dfu, comms::pending_title(comms::Pending::Dfu), Heading::Interrupt));

    // Confirming a firmware upload must not look like confirming a shutdown.
    Glass off;
    draw_confirm(off, prompt(comms::Pending::PowerOff));
    CHECK(reads_heading(off, comms::pending_title(comms::Pending::PowerOff), Heading::Interrupt));
    CHECK_FALSE(reads_heading(off, comms::pending_title(comms::Pending::Dfu), Heading::Interrupt));
}

// The page as the frame lays it out: the detail's lines, a gap, then how to cancel.
struct Expected {
    char detail[kConfirmDetailRows][kConfirmDetailColumns + 1]{};
    char seconds[16]{};
    BodyLine lines[kConfirmDetailRows + 4]{};
    int count{0};
    int consumed{0};

    explicit Expected(const ConfirmSnapshot& snapshot) {
        for (int row = 0; row < kConfirmDetailRows; row++) {
            consumed = take_line(snapshot.detail, consumed, detail[row], kConfirmDetailColumns);
            if (detail[row][0] == 0) break;
            lines[count++] = {detail[row]};
        }
        lines[count++] = {""};
        lines[count++] = {kConfirmCancelLine};
        if (snapshot.seconds_left == 0) {
            lines[count++] = {kConfirmOnceLine};
            return;
        }
        lines[count++] = {kConfirmWaitLine};
        std::snprintf(seconds, sizeof(seconds), "%u S",
                      static_cast<unsigned>(snapshot.seconds_left));
        lines[count++] = {seconds};
    }
};

bool reads_prompt(const Glass& canvas, const ConfirmSnapshot& snapshot) {
    const Expected expected(snapshot);
    return reads_body(canvas, Heading::Interrupt, true, expected.lines, expected.count);
}

TEST_CASE("confirm page: what it will do is on the page, in words, and not clipped") {
    for (comms::Pending pending : kEveryPrompt) {
        Glass canvas;
        const ConfirmSnapshot snapshot = prompt(pending);
        draw_confirm(canvas, snapshot);
        CAPTURE(snapshot.detail);
        CHECK(reads_prompt(canvas, snapshot));
        CHECK(Expected(snapshot).consumed == static_cast<int>(std::strlen(snapshot.detail)));
    }
}

TEST_CASE("confirm page: a detail broken by line feeds keeps one setting to a row") {
    ConfirmSnapshot snapshot = prompt(comms::Pending::Set);
    snapshot.detail = "CALLSIGN F-JXYZ\nUNITS METRIC\nAND 2 MORE";
    Glass canvas;
    draw_confirm(canvas, snapshot);
    CHECK(reads_prompt(canvas, snapshot));
    CHECK(reads_inside_body(canvas, "UNITS METRIC"));
}

TEST_CASE("confirm page: the double press allows on the button's key, and one press cancels") {
    Glass canvas;
    const ConfirmSnapshot snapshot = prompt(comms::Pending::Dfu);
    draw_confirm(canvas, snapshot);
    CHECK(reads_button_key(canvas, kConfirmAllowVerb));
    // The pad does nothing at a prompt, so it shows no key.
    CHECK_FALSE(has_pad_key(canvas));
    CHECK(reads_prompt(canvas, snapshot));
    CHECK(reads_inside_body(canvas, "30 S"));
}

TEST_CASE("confirm page: the seconds left count down, and none left offers the press alone") {
    ConfirmSnapshot snapshot = prompt(comms::Pending::Register);
    snapshot.seconds_left = 7;
    Glass seven;
    draw_confirm(seven, snapshot);
    CHECK(reads_prompt(seven, snapshot));
    CHECK(reads_inside_body(seven, "7 S"));

    snapshot.seconds_left = 0;
    Glass none;
    draw_confirm(none, snapshot);
    CHECK(reads_prompt(none, snapshot));
    CHECK_FALSE(reads_inside_body(none, "0 S"));
}

// Short or long, the message sits in the middle of the glass between the rule and the key.
TEST_CASE("confirm page: the body's ink is centred between the heading's rule and the key") {
    for (comms::Pending pending : {comms::Pending::PowerOff, comms::Pending::Set}) {
        Glass canvas;
        draw_confirm(canvas, prompt(pending));
        int top = -1, bottom = -1;
        for (int y = kInterruptRuleY + 1; y < kKeyTop; y++)
            for (int x = 0; x < Glass::kWidth; x++)
                if (canvas.get_pixel(x, y)) {
                    if (top < 0) top = y;
                    bottom = y;
                }
        const int above = top - (kInterruptRuleY + 1);
        const int below = kKeyTop - 1 - bottom;
        CHECK(above - below >= -1);
        CHECK(above - below <= 1);
    }
}

TEST_CASE("confirm page: it cannot be mistaken for a page a pilot was already on") {
    Glass canvas;
    draw_confirm(canvas, prompt(comms::Pending::Recovery));
    // The large mark is what a page the device pushed wears, and no page a pilot opens does.
    CHECK(
        reads_heading(canvas, comms::pending_title(comms::Pending::Recovery), Heading::Interrupt));
    CHECK_FALSE(
        reads_heading(canvas, comms::pending_title(comms::Pending::Recovery), Heading::Opened));
}
