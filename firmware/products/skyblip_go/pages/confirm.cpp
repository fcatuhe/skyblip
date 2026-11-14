#include "products/skyblip_go/pages/confirm.h"

#include "core/util/format.h"

namespace skyblip::go {

void draw_confirm(ui::Canvas& canvas, const ConfirmSnapshot& snapshot) {
    draw_heading(canvas, snapshot.title, Heading::Interrupt);

    char detail[kConfirmDetailRows][kConfirmDetailColumns + 1];
    BodyLine body[kConfirmDetailRows];
    int count = 0;
    int at = 0;
    for (int row = 0; row < kConfirmDetailRows; row++) {
        at = take_line(snapshot.detail, at, detail[row], kConfirmDetailColumns);
        if (detail[row][0] == 0) break;
        body[count++] = {detail[row]};
    }
    draw_question_body(canvas, Heading::Interrupt, snapshot.seconds_left, body, count);
    draw_keys(canvas, {kConfirmAllowVerb, kConfirmCancelVerb});
}

void draw_question_body(ui::Canvas& canvas, Heading heading, uint32_t seconds_left,
                        const BodyLine* lines, int count) {
    if (seconds_left == 0) {
        draw_body(canvas, heading, /*keys=*/true, lines, count);
        return;
    }
    char countdown[kConfirmCountdownBytes];
    countdown[format_confirm_countdown(countdown, seconds_left)] = 0;
    draw_foot_line(canvas, countdown);
    draw_body_above(canvas, heading, kFootLineY, lines, count);
}

int format_confirm_countdown(char* out, uint32_t seconds_left) {
    int written = format_string(out, kConfirmCountdownWords);
    written += format_unsigned(out + written, seconds_left);
    written += format_string(out + written, " S");
    return written;
}

uint32_t seconds_left(uint32_t asked_ms, uint32_t now_ms, uint32_t ceiling_ms) {
    const uint32_t standing_ms = now_ms - asked_ms;
    if (standing_ms >= ceiling_ms) return 0;
    return (ceiling_ms - standing_ms + 999u) / 1000u;
}

}  // namespace skyblip::go
