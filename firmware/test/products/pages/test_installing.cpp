// The page the glass wears through the swap, read back off the pixels.
#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/confirm.h"
#include "products/skyblip_go/pages/installing.h"

using namespace skyblip;
using namespace skyblip::go;

namespace {

int length(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

bool reads_at(const Glass& fb, int x, int y, const char* text, int scale) {
    Glass expected;
    expected.clear(true);
    expected.draw_text(x, y, text, true, scale);
    for (int dy = 0; dy < 7 * scale; dy++)
        for (int dx = 0; dx < length(text) * kInstallingCellW * scale; dx++)
            if (fb.get_pixel(x + dx, y + dy) != expected.get_pixel(x + dx, y + dy)) return false;
    return true;
}

}  // namespace

TEST_CASE("installing page: it says what is happening and what not to do, unclipped") {
    Glass fb;
    draw_installing(fb);
    CHECK(reads_at(fb, kInstallingLeftX, kInstallingTitleY, kInstallingTitle, 2));
    for (int row = 0; row < kInstallingBodyRows; row++) {
        CHECK(length(kInstallingBody[row]) * kInstallingCellW + kInstallingLeftX <= Glass::kW);
        CHECK(reads_at(fb, kInstallingLeftX, installing_body_y(row), kInstallingBody[row], 1));
    }
    CHECK(installing_body_y(kInstallingBodyRows - 1) + 7 < Glass::kH);
}

TEST_CASE("receiving page: it says the install follows on its own, and how to stop it, unclipped") {
    Glass fb;
    draw_receiving(fb);
    CHECK(reads_at(fb, kInstallingLeftX, kInstallingTitleY, kReceivingTitle, 2));
    for (int row = 0; row < kReceivingBodyRows; row++) {
        CHECK(length(kReceivingBody[row]) * kInstallingCellW + kInstallingLeftX <= Glass::kW);
        CHECK(reads_at(fb, kInstallingLeftX, installing_body_y(row), kReceivingBody[row], 1));
    }
    CHECK_FALSE(reads_at(fb, kInstallingLeftX, kInstallingTitleY, kInstallingTitle, 2));
}

TEST_CASE("installing page: it cannot be read as the prompt it replaced") {
    Glass fb;
    draw_installing(fb);
    CHECK_FALSE(reads_at(fb, kConfirmLeftX + kConfirmCellW, kConfirmAllowY, kConfirmAllowText, 1));
    CHECK_FALSE(
        reads_at(fb, kConfirmLeftX + kConfirmCellW, kConfirmRefuseY, kConfirmRefuseText, 1));
}
