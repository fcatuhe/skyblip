#include "products/skyblip_go/pages/recovery.h"

namespace skyblip::go {

void draw_recovery(ui::Canvas& canvas, ports::RecoveryPath path) {
    draw_heading(canvas, kRecoveryTitle, Heading::Interrupt);
    const bool keys = recovery_button(path)[0] != 0;
    draw_body(canvas, Heading::Interrupt, keys, kRecoveryBody, kRecoveryBodyRows);
    draw_keys(canvas, {{Press::Once, recovery_button(path)}, ""});
}

}  // namespace skyblip::go
