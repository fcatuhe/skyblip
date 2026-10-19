// The page the glass wears while the factory bootloader has the device, read back off the pixels.
#include <cstring>
#include <initializer_list>

#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/confirm.h"
#include "products/skyblip_go/pages/recovery.h"
#include "test/support/glass_text.h"

using namespace skyblip;
using namespace skyblip::go;

namespace {

using ui::kCellW;

bool fits(const char* text, int scale) {
    return kInstallingLeftX + static_cast<int>(std::strlen(text)) * kCellW * scale <= Glass::kW;
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
