// The authorisation prompt, read back off the glass. With BLE pairing off, this
// page is the only thing standing between a stranger's phone and the secondary
// slot, so "there is ink on the panel" is not the claim: "the panel says POWER
// OFF, and says which press allows it" is.
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

TEST_CASE("confirm page: what it will do is on the page, in words, and not clipped") {
    for (comms::Pending pending : kEveryPrompt) {
        Glass canvas;
        draw_confirm(canvas, prompt(pending));
        const char* detail = comms::pending_detail(pending);
        CAPTURE(detail);
        char line[kConfirmDetailColumns + 1];
        int at = 0;
        for (int row = 0; row < kConfirmDetailRows; row++) {
            at = take_line(detail, at, line, kConfirmDetailColumns);
            if (line[0] != 0) CHECK(reads_centred(canvas, confirm_detail_y(row), line));
        }
        CHECK(at == static_cast<int>(std::strlen(detail)));
    }
}

TEST_CASE("confirm page: a detail broken by line feeds keeps one setting to a row") {
    ConfirmSnapshot snapshot = prompt(comms::Pending::Set);
    snapshot.detail = "CALLSIGN F-JXYZ\nUNITS METRIC\nAND 2 MORE";
    Glass canvas;
    draw_confirm(canvas, snapshot);
    CHECK(reads_centred(canvas, confirm_detail_y(0), "CALLSIGN F-JXYZ"));
    CHECK(reads_centred(canvas, confirm_detail_y(1), "UNITS METRIC"));
    CHECK(reads_centred(canvas, confirm_detail_y(2), "AND 2 MORE"));
}

TEST_CASE("confirm page: the double press allows on the button's key, and one press cancels") {
    Glass canvas;
    draw_confirm(canvas, prompt(comms::Pending::Dfu));
    CHECK(reads_button_key(canvas, kConfirmAllowVerb));
    // The pad does nothing at a prompt, so it shows no key.
    CHECK_FALSE(has_pad_key(canvas));
    CHECK(reads_centred(canvas, confirm_cancel_y(0), kConfirmCancelLine));
    CHECK(reads_centred(canvas, confirm_cancel_y(1), "ONCE OR WAIT"));
    // And the page says it will refuse on its own, so a prompt left standing is
    // not read as one waiting indefinitely.
    CHECK(reads_centred(canvas, confirm_cancel_y(2), "30 S."));
}

TEST_CASE("confirm page: the seconds left count down, and none left offers the press alone") {
    ConfirmSnapshot snapshot = prompt(comms::Pending::Register);
    snapshot.seconds_left = 7;
    Glass seven;
    draw_confirm(seven, snapshot);
    CHECK(reads_centred(seven, confirm_cancel_y(2), "7 S."));

    snapshot.seconds_left = 0;
    Glass none;
    draw_confirm(none, snapshot);
    CHECK(reads_centred(none, confirm_cancel_y(1), "ONCE."));
    int ink = 0;
    for (int y = confirm_cancel_y(2); y < confirm_cancel_y(2) + 14; y++)
        for (int x = 0; x < Glass::kWidth; x++) ink += none.get_pixel(x, y) ? 1 : 0;
    CHECK(ink == 0);
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
