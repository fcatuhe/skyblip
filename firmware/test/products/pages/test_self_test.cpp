// The power-on self test, on the glass. On a first flash this is the only thing
// that can name the part that did not answer, so these read the framebuffer back
// rather than count ink: "there is something on the GNSS row" is not the claim
// that matters, "the GNSS row says FAIL" is.
#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/self_test.h"

using namespace skyblip::go;

namespace {

using skyblip::ui::kCellW;

int length(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

// Draw the same text, right-aligned on the same column, into a scratch buffer
// and compare the box it occupies.
bool reads_at(const Glass& canvas, int y, const char* text) {
    const int len = length(text);
    const int x = kSelfTestRightX - len * kCellW;

    Glass expected;
    expected.clear(true);
    expected.draw_text(x, y, text, true, 1);
    for (int dy = 0; dy < 7; dy++)
        for (int dx = 0; dx < len * kCellW; dx++)
            if (canvas.get_pixel(x + dx, y + dy) != expected.get_pixel(x + dx, y + dy))
                return false;
    return true;
}

bool row_reads(const Glass& canvas, int row, const char* verdict) {
    return reads_at(canvas, self_test_row_y(row), verdict);
}

SelfTestSnapshot page(const SelfTestPart* parts, int n, bool flyable) {
    SelfTestSnapshot s;
    s.address = skyblip::settings::address_of(0x5B7E);
    s.reset_reason = "WATCHDOG";
    s.parts = parts;
    s.part_count = n;
    s.flyable = flyable;
    return s;
}

}  // namespace

TEST_CASE("boot: every part on the page carries its own verdict") {
    const SelfTestPart parts[] = {
        {"RADIO", PartState::Pass},
        {"GNSS", PartState::Fail},
        {"BARO", PartState::Absent},
    };
    Glass canvas;
    draw_self_test(canvas, page(parts, 3, /*flyable=*/false));

    CHECK(row_reads(canvas, 0, "PASS"));
    CHECK(row_reads(canvas, 1, "FAIL"));
    CHECK(row_reads(canvas, 2, "NOT FITTED"));

    // An absent part is not a failed one. A unit built without a barometer must
    // not read as a broken unit on a bench.
    CHECK_FALSE(row_reads(canvas, 2, "FAIL"));
    CHECK_FALSE(row_reads(canvas, 0, "FAIL"));
}

TEST_CASE("boot: the page says whether the device may fly, in one word") {
    const SelfTestPart ok[] = {{"RADIO", PartState::Pass}};
    Glass ready;
    draw_self_test(ready, page(ok, 1, /*flyable=*/true));

    const SelfTestPart bad[] = {{"RADIO", PartState::Fail}};
    Glass grounded;
    draw_self_test(grounded, page(bad, 1, /*flyable=*/false));

    // The verdict is reversed out of a filled block, so the two pages differ by
    // far more than the four characters of the badge.
    CHECK(ready.count_black() != grounded.count_black());
    CHECK(grounded.count_black() > 200);
    CHECK(ready.count_black() > 200);
}

TEST_CASE("boot: the reset reason and the identity share the header") {
    const SelfTestPart parts[] = {{"RADIO", PartState::Pass}};
    Glass canvas;
    draw_self_test(canvas, page(parts, 1, /*flyable=*/true));
    CHECK(reads_at(canvas, kSelfTestHeaderY, "WATCHDOG"));

    // A boot that followed a bite must not look like a boot that followed a
    // pilot pressing the button.
    SelfTestSnapshot other = page(parts, 1, /*flyable=*/true);
    other.reset_reason = "POWER ON";
    Glass fresh;
    draw_self_test(fresh, other);
    CHECK(reads_at(fresh, kSelfTestHeaderY, "POWER ON"));
    CHECK_FALSE(reads_at(fresh, kSelfTestHeaderY, "WATCHDOG"));
}

TEST_CASE("boot: a full inventory still fits the panel") {
    SelfTestPart parts[kSelfTestRows];
    for (int i = 0; i < kSelfTestRows; i++) {
        parts[i].name = "PART";
        parts[i].state = PartState::Pass;
    }
    Glass canvas;
    draw_self_test(canvas, page(parts, kSelfTestRows, /*flyable=*/true));

    // The last row and the verdict below it are both on the glass, not clipped
    // off the bottom of a 200-pixel panel.
    CHECK(self_test_row_y(kSelfTestRows - 1) + 7 < Glass::kH);
    CHECK(row_reads(canvas, kSelfTestRows - 1, "PASS"));

    int footer_ink = 0;
    for (int y = self_test_row_y(kSelfTestRows - 1) + 8; y < Glass::kH; y++)
        for (int x = 0; x < Glass::kW; x++) footer_ink += canvas.get_pixel(x, y) ? 1 : 0;
    CHECK(footer_ink > 100);
}

TEST_CASE("boot: the cell's voltage is on the page when there is a gauge to read") {
    const SelfTestPart parts[] = {{"RADIO", PartState::Pass}};
    SelfTestSnapshot with = page(parts, 1, /*flyable=*/true);
    with.battery_valid = true;
    with.battery_mv = 4050;

    Glass canvas;
    draw_self_test(canvas, with);
    Glass without;
    draw_self_test(without, page(parts, 1, /*flyable=*/true));
    CHECK(canvas.count_black() > without.count_black());
}

// ---------------------------------------------------------------------------
// Which part answered, not just whether one did. LilyGO fits either of two
// barometer addresses, one of five e-paper lots and a haptic that may be a
// waveform driver or a motor on a pin, so PASS on its own is a page that cannot
// tell two units apart.
// ---------------------------------------------------------------------------

namespace {

// The same comparison as reads_at, at a stated left edge instead of the right one.
bool reads_from(const Glass& canvas, int x, int y, const char* text) {
    const int len = length(text);
    Glass expected;
    expected.clear(true);
    expected.draw_text(x, y, text, true, 1);
    for (int dy = 0; dy < 7; dy++)
        for (int dx = 0; dx < len * kCellW; dx++)
            if (canvas.get_pixel(x + dx, y + dy) != expected.get_pixel(x + dx, y + dy))
                return false;
    return true;
}

int detail_x(const char* name) { return kSelfTestLeftX + (length(name) + 1) * kCellW; }

}  // namespace

TEST_CASE("boot: a part that could be two parts says which one it is") {
    SelfTestPart parts[] = {
        {"BARO", PartState::Pass, "76"},
        {"PANEL", PartState::Pass, "D67/1942"},
        {"HAPTIC", PartState::Pass, "DRV2605"},
    };
    Glass canvas;
    draw_self_test(canvas, page(parts, 3, /*flyable=*/true));

    // The identity sits with the name and the verdict stays where the eye
    // expects it: a row reads "what it is", then "how it answered".
    CHECK(reads_from(canvas, detail_x("BARO"), self_test_row_y(0), "76"));
    CHECK(reads_from(canvas, detail_x("PANEL"), self_test_row_y(1), "D67/1942"));
    CHECK(reads_from(canvas, detail_x("HAPTIC"), self_test_row_y(2), "DRV2605"));
    CHECK(row_reads(canvas, 0, "PASS"));
    CHECK(row_reads(canvas, 1, "PASS"));
    CHECK(row_reads(canvas, 2, "PASS"));
}

TEST_CASE("boot: a row with no detail is the row it always was") {
    const SelfTestPart plain[] = {{"RADIO", PartState::Pass}};
    Glass without;
    draw_self_test(without, page(plain, 1, /*flyable=*/true));

    SelfTestPart detailed[] = {{"RADIO", PartState::Pass, "1262"}};
    Glass with;
    draw_self_test(with, page(detailed, 1, /*flyable=*/true));

    CHECK(row_reads(without, 0, "PASS"));
    CHECK(reads_from(without, kSelfTestLeftX, self_test_row_y(0), "RADIO"));
    // The detail is the only difference, and it is not on the page that has none.
    CHECK_FALSE(reads_from(without, detail_x("RADIO"), self_test_row_y(0), "1262"));
    CHECK(reads_from(with, detail_x("RADIO"), self_test_row_y(0), "1262"));
}

TEST_CASE("boot: the bus scan is a row of its own, addresses and no verdict") {
    const SelfTestPart parts[] = {{"RADIO", PartState::Pass}, {"BARO", PartState::Pass, "76"}};
    // What a fully fitted Plus answers: IMU, RTC, haptic driver, barometer. Two
    // of those four have no driver anywhere in the tree, which is exactly why the
    // row exists.
    const uint8_t found[] = {0x28, 0x51, 0x5A, 0x76};
    const char* const roles[] = {"IMU", "RTC", "HAPTIC", "BARO"};
    SelfTestSnapshot s = page(parts, 2, /*flyable=*/true);
    s.i2c_addresses = found;
    s.i2c_roles = roles;
    s.i2c_address_count = 4;

    Glass canvas;
    draw_self_test(canvas, s);

    CHECK(reads_from(canvas, kSelfTestLeftX, self_test_row_y(2), "I2C IMU RTC HAPTIC BARO"));
    // No verdict: nothing on this row is a pass or a failure.
    CHECK_FALSE(row_reads(canvas, 2, "PASS"));
    CHECK_FALSE(row_reads(canvas, 2, "FAIL"));
    CHECK_FALSE(row_reads(canvas, 2, "NOT FITTED"));
}

// A part nobody expected is the reason to scan at all, and there is no name for it.
TEST_CASE("boot: an address the board cannot account for reads as its hex") {
    const SelfTestPart parts[] = {{"RADIO", PartState::Pass}};
    const uint8_t found[] = {0x3C, 0x51};
    const char* const roles[] = {nullptr, "RTC"};
    SelfTestSnapshot s = page(parts, 1, /*flyable=*/true);
    s.i2c_addresses = found;
    s.i2c_roles = roles;
    s.i2c_address_count = 2;

    Glass canvas;
    draw_self_test(canvas, s);

    CHECK(reads_from(canvas, kSelfTestLeftX, self_test_row_y(1), "I2C 3C RTC"));
}

// The row is what a bench reads a bus off: it must not run into the row's edge.
TEST_CASE("boot: the bus row stops at the width of a row rather than running off it") {
    const SelfTestPart parts[] = {{"RADIO", PartState::Pass}};
    const uint8_t found[] = {0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    const char* const roles[] = {"HAPTIC", "HAPTIC", "HAPTIC", "HAPTIC",
                                 "HAPTIC", "HAPTIC", "HAPTIC", "HAPTIC"};
    SelfTestSnapshot s = page(parts, 1, /*flyable=*/true);
    s.i2c_addresses = found;
    s.i2c_roles = roles;
    s.i2c_address_count = 8;

    Glass canvas;
    draw_self_test(canvas, s);

    for (int y = self_test_row_y(1); y < self_test_row_y(1) + 7; y++)
        for (int x = kSelfTestRightX; x < kGlassWidth; x++) CHECK_FALSE(canvas.get_pixel(x, y));
}

TEST_CASE("boot: the inventory a real unit reports fits with the bus row on it") {
    // Eleven capabilities and the scan's row: the page the product actually draws.
    SelfTestPart parts[kSelfTestRows - 1];
    for (int i = 0; i < kSelfTestRows - 1; i++) {
        parts[i].name = "PART";
        parts[i].state = PartState::Pass;
    }
    const uint8_t found[] = {0x28, 0x51, 0x5A, 0x76};
    const char* const roles[] = {"IMU", "RTC", "HAPTIC", "BARO"};
    SelfTestSnapshot s = page(parts, kSelfTestRows - 1, /*flyable=*/true);
    s.i2c_addresses = found;
    s.i2c_roles = roles;
    s.i2c_address_count = 4;
    s.battery_valid = true;
    s.battery_mv = 4050;

    Glass canvas;
    draw_self_test(canvas, s);

    CHECK(reads_from(canvas, kSelfTestLeftX, self_test_row_y(kSelfTestRows - 1),
                     "I2C IMU RTC HAPTIC BARO"));
    CHECK(self_test_row_y(kSelfTestRows - 1) + 7 < Glass::kH);

    // And the verdict is still on the glass below all of it.
    int footer_ink = 0;
    for (int y = self_test_row_y(kSelfTestRows - 1) + 8; y < Glass::kH; y++)
        for (int x = 0; x < Glass::kW; x++) footer_ink += canvas.get_pixel(x, y) ? 1 : 0;
    CHECK(footer_ink > 100);
}

TEST_CASE("boot: a unit that last went dark on a flat cell says so beside the cell's voltage") {
    const SelfTestPart parts[] = {{"RADIO", PartState::Pass}};
    const int footer_y = self_test_row_y(1) + kSelfTestDividerGap + kSelfTestFooterGap;
    SelfTestSnapshot s = page(parts, 1, /*flyable=*/true);
    s.battery_valid = true;
    s.battery_mv = 4050;
    s.went_dark_flat = true;
    Glass canvas;
    draw_self_test(canvas, s);
    CHECK(reads_from(canvas, kSelfTestLeftX, footer_y, "BAT 4.05 V WAS FLAT"));

    s.went_dark_flat = false;
    Glass ordinary;
    draw_self_test(ordinary, s);
    CHECK(reads_from(ordinary, kSelfTestLeftX, footer_y, "BAT 4.05 V"));
    CHECK_FALSE(reads_from(ordinary, kSelfTestLeftX, footer_y, "BAT 4.05 V WAS FLAT"));
}
