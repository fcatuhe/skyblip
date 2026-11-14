#include "products/skyblip_go/pages/recovery.h"

namespace skyblip::go {

void draw_recovery(ui::Canvas& canvas, ports::RecoveryPath path) {
    draw_heading(canvas, kRecoveryTitle, Heading::Interrupt);
    draw_centred_lines(canvas, kNoticeBodyY, kRecoveryBody, kRecoveryBodyRows);
    draw_keys(canvas, {{Press::Once, recovery_button(path)}, ""});
}

}  // namespace skyblip::go
