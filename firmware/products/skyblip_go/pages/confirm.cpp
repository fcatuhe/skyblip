#include "products/skyblip_go/pages/confirm.h"

#include "core/util/format.h"

namespace skyblip::go {

void draw_confirm(ui::Canvas& canvas, const ConfirmSnapshot& snapshot) {
    draw_heading(canvas, snapshot.title, Heading::Interrupt);

    char detail[kConfirmDetailRows][kConfirmDetailColumns + 1];
    char seconds[16];
    BodyLine body[kConfirmDetailRows + 4];
    int count = 0;
    int at = 0;
    for (int row = 0; row < kConfirmDetailRows; row++) {
        at = take_line(snapshot.detail, at, detail[row], kConfirmDetailColumns);
        if (detail[row][0] == 0) break;
        body[count++] = {detail[row]};
    }
    body[count++] = {""};
    body[count++] = {kConfirmCancelLine};
    if (snapshot.seconds_left == 0) {
        body[count++] = {kConfirmOnceLine};
    } else {
        body[count++] = {kConfirmWaitLine};
        int written = format_unsigned(seconds, snapshot.seconds_left);
        written += format_string(seconds + written, " S");
        seconds[written] = 0;
        body[count++] = {seconds};
    }
    draw_body(canvas, Heading::Interrupt, /*keys=*/true, body, count);

    draw_keys(canvas, {{Press::Twice, kConfirmAllowVerb}, ""});
}

}  // namespace skyblip::go
