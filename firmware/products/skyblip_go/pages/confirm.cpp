#include "products/skyblip_go/pages/confirm.h"

#include "core/util/format.h"

namespace skyblip::go {

void draw_confirm(ui::Canvas& canvas, const ConfirmSnapshot& snapshot) {
    draw_heading(canvas, snapshot.title, Heading::Interrupt);

    char line[kConfirmDetailColumns + 1];
    int at = 0;
    for (int row = 0; row < kConfirmDetailRows; row++) {
        at = take_line(snapshot.detail, at, line, kConfirmDetailColumns);
        if (line[0] == 0) break;
        draw_centred_line(canvas, confirm_detail_y(row), line);
    }

    draw_centred_line(canvas, confirm_cancel_y(0), kConfirmCancelLine);
    if (snapshot.seconds_left == 0) {
        draw_centred_line(canvas, confirm_cancel_y(1), "ONCE.");
    } else {
        draw_centred_line(canvas, confirm_cancel_y(1), "ONCE OR WAIT");
        char text[16];
        int written = format_unsigned(text, snapshot.seconds_left);
        written += format_string(text + written, " S.");
        text[written] = 0;
        draw_centred_line(canvas, confirm_cancel_y(2), text);
    }

    draw_keys(canvas, {{Press::Twice, kConfirmAllowVerb}, ""});
}

}  // namespace skyblip::go
