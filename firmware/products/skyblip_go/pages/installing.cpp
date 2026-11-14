#include "products/skyblip_go/pages/installing.h"

namespace skyblip::go {

void draw_installing(ui::Canvas& canvas) {
    draw_heading(canvas, kInstallingTitle, Heading::Interrupt);
    draw_centred_lines(canvas, kNoticeBodyY, kInstallingBody, kInstallingBodyRows);
}

void draw_receiving(ui::Canvas& canvas) {
    draw_heading(canvas, kReceivingTitle, Heading::Interrupt);
    draw_centred_lines(canvas, kNoticeBodyY, kReceivingBody, kReceivingBodyRows);
    draw_keys(canvas, {{Press::Once, kReceivingCancelVerb}, ""});
}

}  // namespace skyblip::go
