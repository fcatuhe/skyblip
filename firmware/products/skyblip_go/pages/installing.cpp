#include "products/skyblip_go/pages/installing.h"

namespace skyblip::go {

void draw_notice_heading(ui::Canvas& canvas, const char* header, const char* title) {
    canvas.clear(true);
    canvas.rect(0, 0, kGlassW, 26, true, /*fill=*/true);
    canvas.draw_text(kInstallingLeftX + kInstallingCellW, 6, header, false, 2);

    canvas.draw_text(kInstallingLeftX, kInstallingTitleY, title, true, 2);
    canvas.hline(kInstallingLeftX, kInstallingTitleY + 22, kGlassW - 2 * kInstallingLeftX, true);
}

namespace {
void draw_notice(ui::Canvas& canvas, const char* title, const char* const* body, int rows) {
    draw_notice_heading(canvas, kInstallingHeader, title);
    for (int row = 0; row < rows; row++)
        canvas.draw_text(kInstallingLeftX, installing_body_y(row), body[row], true, 1);
}
}  // namespace

void draw_installing(ui::Canvas& canvas) {
    draw_notice(canvas, kInstallingTitle, kInstallingBody, kInstallingBodyRows);
}

void draw_receiving(ui::Canvas& canvas) {
    draw_notice(canvas, kReceivingTitle, kReceivingBody, kReceivingBodyRows);
}

}  // namespace skyblip::go
