#include "products/skyblip_go/pages/installing.h"

namespace skyblip::go {

void draw_notice_heading(ui::Canvas& fb, const char* header, const char* title) {
    fb.clear(true);
    fb.rect(0, 0, kGlassW, 26, true, /*fill=*/true);
    fb.draw_text(kInstallingLeftX + kInstallingCellW, 6, header, false, 2);

    fb.draw_text(kInstallingLeftX, kInstallingTitleY, title, true, 2);
    fb.hline(kInstallingLeftX, kInstallingTitleY + 22, kGlassW - 2 * kInstallingLeftX, true);
}

namespace {
void draw_notice(ui::Canvas& fb, const char* title, const char* const* body, int rows) {
    draw_notice_heading(fb, kInstallingHeader, title);
    for (int row = 0; row < rows; row++)
        fb.draw_text(kInstallingLeftX, installing_body_y(row), body[row], true, 1);
}
}  // namespace

void draw_installing(ui::Canvas& fb) {
    draw_notice(fb, kInstallingTitle, kInstallingBody, kInstallingBodyRows);
}

void draw_receiving(ui::Canvas& fb) {
    draw_notice(fb, kReceivingTitle, kReceivingBody, kReceivingBodyRows);
}

}  // namespace skyblip::go
