// The page the glass wears while the factory bootloader has the device, read back off the pixels.
#include <initializer_list>

#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/confirm.h"
#include "products/skyblip_go/pages/recovery.h"

using namespace skyblip;
using namespace skyblip::go;

namespace {

using ui::kCellW;

int length(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

bool reads_at(const Glass& canvas, int x, int y, const char* text, int scale) {
    Glass expected;
    expected.clear(true);
    expected.draw_text(x, y, text, true, scale);
    for (int dy = 0; dy < 7 * scale; dy++)
        for (int dx = 0; dx < length(text) * kCellW * scale; dx++)
            if (canvas.get_pixel(x + dx, y + dy) != expected.get_pixel(x + dx, y + dy))
                return false;
    return true;
}

bool fits(const char* text, int scale) {
    return kInstallingLeftX + length(text) * kCellW * scale <= Glass::kW;
}

}  // namespace

TEST_CASE("recovery page: it names the drive, the file and the way back, unclipped") {
    for (const ports::RecoveryPath path :
         {ports::RecoveryPath::Rebooted, ports::RecoveryPath::PowerOffToFinish}) {
        Glass canvas;
        draw_recovery(canvas, path);
        CHECK(fits(kRecoveryTitle, 2));
        CHECK(reads_at(canvas, kInstallingLeftX, kInstallingTitleY, kRecoveryTitle, 2));
        CHECK(fits(recovery_entry(path), 1));
        CHECK(reads_at(canvas, kInstallingLeftX, installing_body_y(0), recovery_entry(path), 1));
        for (int row = 0; row < kRecoveryBodyRows; row++) {
            CHECK(fits(kRecoveryBody[row], 1));
            CHECK(reads_at(canvas, kInstallingLeftX, installing_body_y(row + 1), kRecoveryBody[row],
                           1));
        }
        CHECK(installing_body_y(kRecoveryBodyRows) + 7 < Glass::kH);
    }
}

TEST_CASE("recovery page: a reboot says the bootloader runs, a power off asks for the press") {
    Glass rebooted;
    draw_recovery(rebooted, ports::RecoveryPath::Rebooted);
    Glass powered_off;
    draw_recovery(powered_off, ports::RecoveryPath::PowerOffToFinish);

    CHECK(reads_at(rebooted, kInstallingLeftX, installing_body_y(0), kRecoveryRunning, 1));
    CHECK(reads_at(powered_off, kInstallingLeftX, installing_body_y(0), kRecoveryAwaitsPress, 1));
    CHECK_FALSE(
        reads_at(rebooted, kInstallingLeftX, installing_body_y(0), kRecoveryAwaitsPress, 1));
    CHECK_FALSE(reads_at(powered_off, kInstallingLeftX, installing_body_y(0), kRecoveryRunning, 1));
}

TEST_CASE("recovery page: it cannot be read as the prompt it replaced") {
    Glass canvas;
    draw_recovery(canvas, ports::RecoveryPath::Rebooted);
    CHECK_FALSE(reads_at(canvas, kConfirmLeftX + kCellW, kConfirmAllowY, kConfirmAllowText, 1));
    CHECK_FALSE(reads_at(canvas, kConfirmLeftX + kCellW, kConfirmRefuseY, kConfirmRefuseText, 1));
}
