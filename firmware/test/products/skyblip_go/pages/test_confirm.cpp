// The authorisation prompt, read back off the glass. With BLE pairing off, this
// page is the only thing standing between a stranger's phone and the secondary
// slot, so "there is ink on the glass" is not the claim: "the glass says POWER
// OFF, and says which press allows it" is.
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>

#include "doctest/doctest.h"
#include "products/common/services/config_service.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/confirm.h"
#include "test/support/frame_glass.h"

using namespace skyblip;
using namespace skyblip::go;

namespace {

ConfirmSnapshot prompt(common::Pending pending) {
    ConfirmSnapshot snapshot;
    snapshot.title = common::pending_title(pending);
    snapshot.detail = common::pending_detail(pending);
    snapshot.seconds_left = common::kPromptCeilingMs / 1000;
    return snapshot;
}

constexpr common::Pending kEveryPrompt[] = {
    common::Pending::Set,      common::Pending::Dfu,      common::Pending::Recovery,
    common::Pending::PowerOff, common::Pending::EraseLog, common::Pending::GnssColdStart,
    common::Pending::Register, common::Pending::Capture};

}  // namespace

TEST_CASE("confirm page: the pilot is told which operation they are authorizing") {
    Glass dfu;
    draw_confirm(dfu, prompt(common::Pending::Dfu));
    CHECK(reads_heading(dfu, common::pending_title(common::Pending::Dfu), Heading::Interrupt));

    // Confirming a firmware upload must not look like confirming a shutdown.
    Glass off;
    draw_confirm(off, prompt(common::Pending::PowerOff));
    CHECK(reads_heading(off, common::pending_title(common::Pending::PowerOff), Heading::Interrupt));
    CHECK_FALSE(
        reads_heading(off, common::pending_title(common::Pending::Dfu), Heading::Interrupt));
}

// The page as the frame lays it out: the detail's lines centred above the countdown, which sits
// over the keys, or above the keys when there is no countdown.
struct Expected {
    char detail[kConfirmDetailRows][kConfirmDetailColumns + 1]{};
    char countdown[kConfirmCountdownBytes]{};
    BodyLine lines[kConfirmDetailRows]{};
    int count{0};
    int consumed{0};

    explicit Expected(const ConfirmSnapshot& snapshot) {
        for (int row = 0; row < kConfirmDetailRows; row++) {
            consumed = take_line(snapshot.detail, consumed, detail[row], kConfirmDetailColumns);
            if (detail[row][0] == 0) break;
            lines[count++] = {detail[row]};
        }
        if (snapshot.seconds_left == 0) return;
        std::snprintf(countdown, sizeof(countdown), "CANCELS IN %u S",
                      static_cast<unsigned>(snapshot.seconds_left));
    }
};

bool reads_prompt(const Glass& canvas, const ConfirmSnapshot& snapshot) {
    const Expected expected(snapshot);
    if (snapshot.seconds_left == 0)
        return reads_body(canvas, Heading::Interrupt, true, expected.lines, expected.count);
    return reads_body_above(canvas, Heading::Interrupt, kFootLineY, expected.lines,
                            expected.count) &&
           reads_foot_line(canvas, expected.countdown);
}

TEST_CASE("confirm page: what it will do is on the page, in words, and not clipped") {
    for (common::Pending pending : kEveryPrompt) {
        Glass canvas;
        const ConfirmSnapshot snapshot = prompt(pending);
        draw_confirm(canvas, snapshot);
        CAPTURE(snapshot.detail);
        CHECK(reads_prompt(canvas, snapshot));
        CHECK(Expected(snapshot).consumed == static_cast<int>(std::strlen(snapshot.detail)));
    }
}

// Arming spends the pilot's partition, so the glass names the profile and the flights it erases.
TEST_CASE("confirm page: a capture names its profile and the flights it erases, not clipped" *
          doctest::test_suite("main")) {
    char detail[common::kPromptDetailBytes];
    for (const comms::CaptureProfile profile :
         {comms::CaptureProfile::Full, comms::CaptureProfile::PowerRun,
          comms::CaptureProfile::FlightRun, comms::CaptureProfile::Stop}) {
        for (const uint32_t evicts : {0u, 1u, 99u}) {
            detail[common::format_capture_detail(detail, profile, evicts)] = 0;
            ConfirmSnapshot snapshot = prompt(common::Pending::Capture);
            snapshot.detail = detail;
            Glass canvas;
            draw_confirm(canvas, snapshot);
            CAPTURE(snapshot.detail);
            CHECK(reads_prompt(canvas, snapshot));
            CHECK(Expected(snapshot).consumed == static_cast<int>(std::strlen(snapshot.detail)));
        }
    }
    detail[common::format_capture_detail(detail, comms::CaptureProfile::Full, 3)] = 0;
    CHECK(std::string(detail) == "START A FULL CAPTURE, ERASES 3 FLIGHTS");
    detail[common::format_capture_detail(detail, comms::CaptureProfile::PowerRun, 1)] = 0;
    CHECK(std::string(detail) == "START A POWER RUN, ERASES 1 FLIGHT");
}

TEST_CASE("confirm page: a detail broken by line feeds keeps one setting to a row") {
    ConfirmSnapshot snapshot = prompt(common::Pending::Set);
    snapshot.detail = "CALLSIGN F-JXYZ\nUNITS METRIC\nAND 2 MORE";
    Glass canvas;
    draw_confirm(canvas, snapshot);
    CHECK(reads_prompt(canvas, snapshot));
    CHECK(reads_inside_body(canvas, "UNITS METRIC"));
}

TEST_CASE("confirm page: one press accepts on the button's key, the pad's key cancels" *
          doctest::test_suite("main")) {
    Glass canvas;
    const ConfirmSnapshot snapshot = prompt(common::Pending::Dfu);
    draw_confirm(canvas, snapshot);
    CHECK(reads_button_key(canvas, kConfirmAllowVerb));
    CHECK(reads_pad_key(canvas, kConfirmCancelVerb));
    CHECK(reads_prompt(canvas, snapshot));
    CHECK(reads_foot_line(canvas, "CANCELS IN 30 S"));
}

TEST_CASE("confirm page: the seconds left count down, and none left leaves the keys alone") {
    ConfirmSnapshot snapshot = prompt(common::Pending::Register);
    snapshot.seconds_left = 7;
    Glass seven;
    draw_confirm(seven, snapshot);
    CHECK(reads_prompt(seven, snapshot));
    CHECK(reads_foot_line(seven, "CANCELS IN 7 S"));

    snapshot.seconds_left = 0;
    Glass none;
    draw_confirm(none, snapshot);
    CHECK(reads_prompt(none, snapshot));
    CHECK_FALSE(reads_inside_body(none, "0 S"));
    CHECK(reads_button_key(none, kConfirmAllowVerb));
    CHECK(reads_pad_key(none, kConfirmCancelVerb));
}

// Short or long, the message sits in the middle of the glass between the rule and the countdown.
TEST_CASE("confirm page: the body's ink is centred between the rule and the countdown") {
    for (common::Pending pending : {common::Pending::PowerOff, common::Pending::Set}) {
        Glass canvas;
        draw_confirm(canvas, prompt(pending));
        int top = -1, bottom = -1;
        for (int y = kInterruptRuleY + 1; y < kFootLineY; y++)
            for (int x = 0; x < Glass::kWidth; x++)
                if (canvas.get_pixel(x, y)) {
                    if (top < 0) top = y;
                    bottom = y;
                }
        const int above = top - (kInterruptRuleY + 1);
        const int below = kFootLineY - 1 - bottom;
        CHECK(above - below >= -1);
        CHECK(above - below <= 1);
    }
}

// The countdown is pinned over the keys, whatever the detail's length.
TEST_CASE("confirm page: the countdown sits on the line just above the keys") {
    for (common::Pending pending : {common::Pending::PowerOff, common::Pending::Set}) {
        Glass canvas;
        draw_confirm(canvas, prompt(pending));
        CHECK(reads_foot_line(canvas, "CANCELS IN 30 S"));
    }
    CHECK(kFootLineY + ui::kCharacterHeight * kFrameScale < kKeyTop);
}

TEST_CASE("confirm page: it cannot be mistaken for a page a pilot was already on") {
    Glass canvas;
    draw_confirm(canvas, prompt(common::Pending::Recovery));
    // The large mark is what a page the device pushed wears, and no page a pilot opens does.
    CHECK(reads_heading(canvas, common::pending_title(common::Pending::Recovery),
                        Heading::Interrupt));
    CHECK_FALSE(
        reads_heading(canvas, common::pending_title(common::Pending::Recovery), Heading::Opened));
}
