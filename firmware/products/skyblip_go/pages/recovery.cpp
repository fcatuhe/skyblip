#include "products/skyblip_go/pages/recovery.h"

namespace skyblip::go {

void draw_recovery(ui::Canvas& fb, ports::RecoveryPath path) {
    draw_notice_heading(fb, kRecoveryHeader, kRecoveryTitle);
    fb.draw_text(kInstallingLeftX, installing_body_y(0), recovery_entry(path), true, 1);
    for (int row = 0; row < kRecoveryBodyRows; row++)
        fb.draw_text(kInstallingLeftX, installing_body_y(row + 1), kRecoveryBody[row], true, 1);
}

}  // namespace skyblip::go
