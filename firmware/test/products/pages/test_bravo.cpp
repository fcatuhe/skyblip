// The BRAVO page as drawn and as a thumb walks it: the list, the confirm, the wait, the refusals.
#include <cstdint>
#include <string>

#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/bravo.h"
#include "products/skyblip_go/pages/menu.h"
#include "products/skyblip_go/settings.h"
#include "test/support/frame_glass.h"
#include "test/support/glass_text.h"

using namespace skyblip;
using namespace skyblip::go;

namespace {

bool centred_reads(const Glass& canvas, const char* text, int scale) {
    return reads_in(canvas, text, centred_x(text, scale), 0,
                    centred_x(text, scale) + text_cells(text) * ui::kCellWidth * scale,
                    Glass::kHeight, scale);
}

BravoView scanning(int found) {
    BravoView view;
    view.step = BravoStep::Scanning;
    view.found_count = found;
    view.found_tags[0] = 0x3F2A;
    view.found_tags[1] = 0x01B7;
    return view;
}

Glass drawn(const BravoSnapshot& snapshot) {
    Glass canvas;
    draw_bravo(canvas, snapshot);
    return canvas;
}

}  // namespace

TEST_CASE("bravo page: the list reads each Bravo heard by its tag, the focus barred") {
    BravoSnapshot snapshot;
    snapshot.view = scanning(2);
    snapshot.focus = 0;
    Glass canvas = drawn(snapshot);
    CHECK(reads_heading(canvas, "BRAVO", Heading::Opened));
    CHECK(reads_at(canvas, kMenuRightX - 4 * kMenuCellWidth, menu_line_text_y(0), "3F2A",
                   kMenuScale, /*ink=*/false));
    CHECK(reads_at(canvas, kMenuRightX - 4 * kMenuCellWidth, menu_line_text_y(1), "01B7",
                   kMenuScale));
    CHECK(reads_button_key(canvas, "PAIR"));
    CHECK(reads_pad_key(canvas, "NEXT"));

    // On the last Bravo the pad leaves the page, and its key says so.
    snapshot.focus = 1;
    canvas = drawn(snapshot);
    CHECK(reads_pad_key(canvas, "BACK"));
}

TEST_CASE("bravo page: a scan that heard nothing yet says so, and offers only the way back") {
    BravoSnapshot snapshot;
    snapshot.view = scanning(0);
    const Glass canvas = drawn(snapshot);
    CHECK(centred_reads(canvas, "NO BRAVO FOUND", kMenuScale));
    CHECK(centred_reads(canvas, "START A BRAVO", kMenuScale));
    CHECK(centred_reads(canvas, "NEAR THIS GO", kMenuScale));
    CHECK_FALSE(has_button_key(canvas));
    CHECK(reads_pad_key(canvas, "BACK"));
}

TEST_CASE("bravo page: confirm, then the wait for Bravo's button, name the tag") {
    BravoSnapshot snapshot;
    snapshot.view = scanning(1);
    snapshot.confirming = true;
    Glass canvas = drawn(snapshot);
    CHECK(reads_heading(canvas, "PAIR BRAVO", Heading::Opened));
    CHECK(centred_reads(canvas, "3F2A", 4));
    CHECK(reads_button_key(canvas, "PAIR"));
    CHECK(reads_pad_key(canvas, "CANCEL"));

    snapshot.confirming = false;
    snapshot.view.step = BravoStep::Waiting;
    snapshot.view.pairing_tag = 0x3F2A;
    canvas = drawn(snapshot);
    CHECK(centred_reads(canvas, "3F2A", 4));
    CHECK(centred_reads(canvas, "PUSH THE BUTTON", kMenuScale));
    CHECK(centred_reads(canvas, "ON BRAVO", kMenuScale));
    CHECK_FALSE(has_button_key(canvas));
    CHECK(reads_pad_key(canvas, "CANCEL"));
}

TEST_CASE("bravo page: each refusal says why") {
    BravoSnapshot snapshot;
    snapshot.view.step = BravoStep::Refused;
    snapshot.view.refusal = BravoRefusal::PhonesConnected;
    Glass canvas = drawn(snapshot);
    CHECK(centred_reads(canvas, "3 PHONES", kMenuScale));
    CHECK(centred_reads(canvas, "DISCONNECT ONE", kMenuScale));
    CHECK(centred_reads(canvas, "TO PAIR", kMenuScale));
    // A retry would meet the same three phones, so the button offers none.
    CHECK_FALSE(has_button_key(canvas));

    snapshot.view.refusal = BravoRefusal::NarrowLink;
    canvas = drawn(snapshot);
    CHECK(centred_reads(canvas, "THAN 126 BYTES", kMenuScale));

    snapshot.view.refusal = BravoRefusal::NoAnswer;
    canvas = drawn(snapshot);
    CHECK(centred_reads(canvas, "NO DATA", kMenuScale));
    CHECK(reads_button_key(canvas, "PAIR"));
    CHECK(reads_pad_key(canvas, "BACK"));

    snapshot.view.refusal = BravoRefusal::UnknownProtocol;
    canvas = drawn(snapshot);
    CHECK(centred_reads(canvas, "UNKNOWN", kMenuScale));
    CHECK(centred_reads(canvas, "UPDATE THE GO OR", kMenuScale));
}

TEST_CASE("bravo page: a paired Go reads its Bravo's tag and whether it is connected") {
    BravoSnapshot snapshot;
    snapshot.view.paired = true;
    snapshot.view.paired_tag = 0x3F2A;
    snapshot.view.link = BravoLink::Up;
    Glass canvas = drawn(snapshot);
    CHECK(centred_reads(canvas, "3F2A", 4));
    CHECK(centred_reads(canvas, "CONNECTED", kMenuScale));
    CHECK(reads_button_key(canvas, "UNPAIR"));
    CHECK(reads_pad_key(canvas, "BACK"));

    snapshot.view.link = BravoLink::Unsupported;
    canvas = drawn(snapshot);
    CHECK(centred_reads(canvas, "UPDATE NECESSARY", kMenuScale));

    snapshot.unpairing = true;
    canvas = drawn(snapshot);
    CHECK(reads_heading(canvas, "UNPAIR BRAVO", Heading::Opened));
    CHECK(reads_pad_key(canvas, "KEEP"));
}

TEST_CASE("bravo page: the own-ICAO notice says what shows and where to set it") {
    Glass canvas;
    draw_own_icao_notice(canvas);
    CHECK(reads_heading(canvas, kOwnIcaoNoticeTitle, Heading::Interrupt));
    CHECK(centred_reads(canvas, "YOUR TRANSPONDER", kMenuScale));
    CHECK(centred_reads(canvas, "AIRCRAFT", kMenuScale));
    CHECK(reads_button_key(canvas, "CLOSE"));
    CHECK(reads_pad_key(canvas, "CLOSE"));
}

TEST_CASE("bravo page: the notice of a pin Bravo does not hold says the Go dropped it too") {
    Glass canvas;
    draw_bravo_forgot_notice(canvas);
    CHECK(reads_heading(canvas, "BRAVO", Heading::Interrupt));
    CHECK(centred_reads(canvas, "BRAVO IS NOT", kMenuScale));
    CHECK(centred_reads(canvas, "THE GO REMOVED", kMenuScale));
    CHECK(centred_reads(canvas, "PAIR AGAIN IN", kMenuScale));
}

TEST_CASE("bravo page: the pad walks the list and leaves past its end, the button confirms first") {
    BravoPage page;
    page.open(0);
    const BravoView view = scanning(2);
    CHECK(page.button(view, 10) == BravoAction::Redraw);
    CHECK(page.confirming());
    CHECK(page.pad(view, 20) == BravoAction::Redraw);
    CHECK_FALSE(page.confirming());
    CHECK(page.pad(view, 30) == BravoAction::Redraw);
    CHECK(page.focus() == 1);
    CHECK(page.button(view, 40) == BravoAction::Redraw);
    CHECK(page.button(view, 50) == BravoAction::Pair);
    CHECK(page.focus() == 1);
    CHECK(page.pad(view, 60) == BravoAction::Leave);
}

TEST_CASE("bravo page: an empty list takes no press, and the pad leaves it") {
    BravoPage page;
    page.open(0);
    CHECK(page.button(scanning(0), 10) == BravoAction::None);
    CHECK(page.pad(scanning(0), 20) == BravoAction::Leave);
}

TEST_CASE("bravo page: a pairing under way is cancelled by the pad and never idles out") {
    BravoPage page;
    page.open(0);
    BravoView view;
    view.step = BravoStep::Waiting;
    CHECK(page.button(view, 10) == BravoAction::None);
    CHECK(page.idle(view, MenuEditor::kIdleReturnHoldMs + 20) == BravoAction::None);
    CHECK(page.pad(view, 30) == BravoAction::Cancel);

    view.step = BravoStep::Scanning;
    CHECK(page.idle(view, 30 + MenuEditor::kIdleReturnHoldMs) == BravoAction::Leave);
}

TEST_CASE("bravo page: a refusal retries on the button, but three phones wait for a slot") {
    BravoPage page;
    page.open(0);
    BravoView view;
    view.step = BravoStep::Refused;
    view.refusal = BravoRefusal::LinkLost;
    CHECK(page.button(view, 10) == BravoAction::Retry);
    view.refusal = BravoRefusal::PhonesConnected;
    CHECK(page.button(view, 20) == BravoAction::None);
    CHECK(page.pad(view, 30) == BravoAction::Leave);
}

TEST_CASE("bravo page: unpairing takes a second press, and the pad keeps the pin") {
    BravoPage page;
    page.open(0);
    BravoView view;
    view.paired = true;
    CHECK(page.button(view, 10) == BravoAction::Redraw);
    CHECK(page.unpairing());
    CHECK(page.pad(view, 20) == BravoAction::Redraw);
    CHECK_FALSE(page.unpairing());
    CHECK(page.pad(view, 30) == BravoAction::Leave);
    CHECK(page.button(view, 40) == BravoAction::Redraw);
    CHECK(page.button(view, 50) == BravoAction::Unpair);
}

TEST_CASE("bravo page: the BRAVO row reads NONE unpaired and the tag once paired") {
    MenuValues values;
    values.settings = defaults();
    char value[kMenuValueBytes];
    menu_row_value(value, MenuRow::Bravo, values);
    CHECK(std::string(value) == "NONE");
    values.settings.bravo = events::BluetoothAddress{1, {0x2A, 0x3F, 0x11, 0x22, 0x33, 0xC4}};
    menu_row_value(value, MenuRow::Bravo, values);
    CHECK(std::string(value) == "3F2A");
    CHECK(page_behind(MenuRow::Bravo) == Page::Bravo);
    CHECK(menu_owner(Page::Bravo) == Page::Radar);
    CHECK_FALSE(diagnostic(Page::Bravo));
}
