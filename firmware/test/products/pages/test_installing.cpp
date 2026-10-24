// The page the glass wears through the swap, read back off the pixels.
#include <cstring>

#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/confirm.h"
#include "products/skyblip_go/pages/installing.h"
#include "test/support/glass_text.h"

using namespace skyblip;
using namespace skyblip::go;

namespace {

using ui::kCellWidth;

}  // namespace

TEST_CASE("installing page: it says what is happening and what not to do, unclipped") {
    Glass canvas;
    draw_installing(canvas);
    CHECK(reads_at(canvas, kInstallingLeftX, kInstallingTitleY, kInstallingTitle, 2));
    for (int row = 0; row < kInstallingBodyRows; row++) {
        CHECK(static_cast<int>(std::strlen(kInstallingBody[row])) * kCellWidth + kInstallingLeftX <=
              Glass::kWidth);
        CHECK(reads_at(canvas, kInstallingLeftX, installing_body_y(row), kInstallingBody[row], 1));
    }
    CHECK(installing_body_y(kInstallingBodyRows - 1) + 7 < Glass::kHeight);
}

TEST_CASE("receiving page: it says the install follows on its own, and how to stop it, unclipped") {
    Glass canvas;
    draw_receiving(canvas);
    CHECK(reads_at(canvas, kInstallingLeftX, kInstallingTitleY, kReceivingTitle, 2));
    for (int row = 0; row < kReceivingBodyRows; row++) {
        CHECK(static_cast<int>(std::strlen(kReceivingBody[row])) * kCellWidth + kInstallingLeftX <=
              Glass::kWidth);
        CHECK(reads_at(canvas, kInstallingLeftX, installing_body_y(row), kReceivingBody[row], 1));
    }
    CHECK_FALSE(reads_at(canvas, kInstallingLeftX, kInstallingTitleY, kInstallingTitle, 2));
}

TEST_CASE("installing page: it cannot be read as the prompt it replaced") {
    Glass canvas;
    draw_installing(canvas);
    CHECK_FALSE(reads_at(canvas, kConfirmLeftX + kCellWidth, kConfirmAllowY, kConfirmAllowText, 1));
    CHECK_FALSE(
        reads_at(canvas, kConfirmLeftX + kCellWidth, kConfirmRefuseY, kConfirmRefuseText, 1));
}
