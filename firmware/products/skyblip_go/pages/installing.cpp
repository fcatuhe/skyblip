#include "products/skyblip_go/pages/installing.h"

namespace skyblip::go {

void draw_installing(ui::Canvas& canvas) {
    draw_heading(canvas, kInstallingTitle, Heading::Interrupt);
    draw_body(canvas, Heading::Interrupt, /*keys=*/false, kInstallingBody, kInstallingBodyRows);
}

void draw_receiving(ui::Canvas& canvas) {
    draw_heading(canvas, kReceivingTitle, Heading::Interrupt);
    draw_body(canvas, Heading::Interrupt, /*keys=*/true, kReceivingBody, kReceivingBodyRows);
    draw_keys(canvas, {{Press::Once, kReceivingCancelVerb}, ""});
}

}  // namespace skyblip::go
