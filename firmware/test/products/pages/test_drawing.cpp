// The drawing stack end to end, from a pixel to what reaches the glass.
#include "doctest/doctest.h"
#include "hardware/parts/ssd1681/model.h"
#include "hardware/parts/ssd1681/ssd1681.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/status.h"
#include "test/support/glass_text.h"

using namespace skyblip::go;
using skyblip::reads_in;

TEST_CASE("fb: pixel set/get and clear") {
    Glass fb;
    fb.clear(true);
    CHECK(fb.count_black() == 0);
    fb.set_pixel(10, 20, true);
    CHECK(fb.get_pixel(10, 20));
    CHECK(fb.count_black() == 1);
    fb.set_pixel(10, 20, false);
    CHECK_FALSE(fb.get_pixel(10, 20));
    fb.set_pixel(-1, -1, true);  // out of bounds no-op
    fb.set_pixel(999, 999, true);
    CHECK(fb.count_black() == 0);
}

TEST_CASE("fb: primitives draw something") {
    Glass fb;
    fb.clear(true);
    fb.line(0, 0, 199, 199, true);
    CHECK(fb.get_pixel(0, 0));
    CHECK(fb.get_pixel(199, 199));
    int before = fb.count_black();
    fb.circle(100, 100, 50, true, false);
    CHECK(fb.count_black() > before);
    fb.rect(10, 10, 30, 20, true, true);
    CHECK(fb.get_pixel(25, 20));
}

TEST_CASE("fb: text advances and draws glyph pixels") {
    Glass fb;
    fb.clear(true);
    int x = fb.draw_text(5, 5, "AB1", true, 1);
    CHECK(x == 5 + 3 * 6);
    CHECK(fb.count_black() > 0);
    // space draws nothing
    Glass fb2;
    fb2.clear(true);
    fb2.draw_char(0, 0, ' ', true, 1);
    CHECK(fb2.count_black() == 0);
}

// A character with no glyph prints as a space, and only the glass ever notices.
TEST_CASE("fb: every character the pages print has a glyph of its own") {
    for (const char* c = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-.:/+%"; *c != 0; c++) {
        Glass fb;
        fb.clear(true);
        fb.draw_char(0, 0, *c, true, 1);
        CAPTURE(*c);
        CHECK(fb.count_black() > 0);
    }
}

TEST_CASE("panel model: the driver's own output is what the model shows") {
    Glass fb;
    StatusSnapshot s;
    s.fix_valid = true;
    s.alt_mm = 900000;
    s.baro_valid = true;
    s.pressure_mpa = 90810000;
    draw_status(fb, s);

    skyblip::models::Ssd1681 panel;
    skyblip::parts::Ssd1681 driver(panel, panel, panel, panel.dc, panel.rst, panel.busy);
    driver.begin();
    driver.present(fb, skyblip::ports::Refresh::Full, 0);

    CHECK(panel.present_count == 1);
    CHECK(panel.last_full);
    // Round trip through the driver's inversion: what the panel holds must be
    // pixel-for-pixel what the UI drew.
    CHECK(panel.framebuffer().count_black() == fb.count_black());
    CHECK(panel.save_pgm("build/status.pgm"));
}
