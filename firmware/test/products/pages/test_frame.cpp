// The frame every asking page is set in: the mark beside the title, and the two keys in their
// places.
#include <initializer_list>
#include <string>

#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/frame.h"
#include "test/support/frame_glass.h"
#include "ui/widgets/mark.h"

using namespace skyblip;
using namespace skyblip::go;

TEST_CASE("frame: the mark's ink is centred on the title's capitals, at either size") {
    for (const Heading heading : {Heading::Opened, Heading::Interrupt}) {
        Glass canvas;
        draw_heading(canvas, "POWER OFF", heading);
        CHECK(reads_heading(canvas, "POWER OFF", heading));

        int top = Glass::kHeight, bottom = -1;
        const int title_x = heading_title_x("POWER OFF", heading);
        const int rule_y = heading == Heading::Interrupt ? kInterruptRuleY : kOpenedRuleY;
        for (int y = 0; y < rule_y; y++)
            for (int x = 0; x < title_x - 2; x++)
                if (canvas.get_pixel(x, y)) {
                    if (y < top) top = y;
                    if (y > bottom) bottom = y;
                }
        const int cap_top = heading_title_y(heading);
        const int cap_bottom = cap_top + ui::kCharacterHeight * kFrameScale - 1;
        const int mark_twice_middle = top + bottom;
        const int caps_twice_middle = cap_top + cap_bottom;
        CHECK(mark_twice_middle - caps_twice_middle >= -1);
        CHECK(mark_twice_middle - caps_twice_middle <= 1);
    }
}

TEST_CASE("frame: the pilot's own pages wear the small mark, the device's own the large one") {
    Glass opened;
    draw_heading(opened, "SETTINGS", Heading::Opened);
    Glass pushed;
    draw_heading(pushed, "SETTINGS", Heading::Interrupt);
    CHECK(reads_heading(opened, "SETTINGS", Heading::Opened));
    CHECK(reads_heading(pushed, "SETTINGS", Heading::Interrupt));
    CHECK(ui::mark_extent(ui::MarkSize::Large).height >
          ui::mark_extent(ui::MarkSize::Small).height);
    CHECK(heading_title_y(Heading::Interrupt) > heading_title_y(Heading::Opened));
}

TEST_CASE("frame: the button's key is filled on the left, the pad's is open on the right") {
    Glass canvas;
    canvas.clear(true);
    draw_keys(canvas, {{Press::Once, "PAIR"}, "BACK"});
    CHECK(reads_button_key(canvas, "PAIR"));
    CHECK(reads_pad_key(canvas, "BACK"));
    // Filled: the middle of the button's key is ink where no letter stands; the pad's is glass.
    CHECK(canvas.get_pixel(kButtonKeyX + 3, kKeyTop + kKeyHeight / 2));
    CHECK_FALSE(canvas.get_pixel(kPadKeyX + 3, kKeyTop + kKeyHeight / 2));
    CHECK(kButtonKeyX < kPadKeyX);
}

TEST_CASE("frame: a contact with nothing to do has no key, and the other stays in its place") {
    Glass pad_only;
    pad_only.clear(true);
    draw_keys(pad_only, {{}, "CANCEL"});
    CHECK_FALSE(has_button_key(pad_only));
    CHECK(reads_pad_key(pad_only, "CANCEL"));

    Glass button_only;
    button_only.clear(true);
    draw_keys(button_only, {{Press::Twice, "ACCEPT"}, ""});
    CHECK(reads_button_key(button_only, "ACCEPT"));
    CHECK_FALSE(has_pad_key(button_only));
}

TEST_CASE("frame: a line breaks on a space or a line feed, and never inside a word") {
    char line[kFrameColumns + 1];
    const char* text = "SET THE TRANSPONDER TO ALT.";
    int at = take_line(text, 0, line, kFrameColumns);
    CHECK(std::string(line) == "SET THE");
    at = take_line(text, at, line, kFrameColumns);
    CHECK(std::string(line) == "TRANSPONDER TO");
    at = take_line(text, at, line, kFrameColumns);
    CHECK(std::string(line) == "ALT.");
    take_line("ONE\nTWO", 0, line, kFrameColumns);
    CHECK(std::string(line) == "ONE");
}
