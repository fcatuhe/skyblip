#include "products/skyblip_go/pages/confirm.h"

#include <cstring>

#include "core/util/format.h"

namespace skyblip::go {

namespace {

constexpr int kLeft = kConfirmLeftX;
using ui::kCellW;

int break_within(const char* text, int from, int to) {
    for (int i = from; i < to; i++)
        if (text[i] == '\n') return i;
    return -1;
}

// One line of at most kConfirmDetailCols characters, broken on a space so a
// sentence a pilot has to read in a hurry does not split a word, or where the
// detail breaks it with '\n', one setting to a row.
int take_line(const char* text, int from, char* out) {
    const int length = static_cast<int>(std::strlen(text));
    if (from >= length) {
        out[0] = 0;
        return from;
    }
    int end = from + kConfirmDetailCols;
    const int newline = break_within(text, from, end < length ? end + 1 : length);
    if (newline >= 0) {
        end = newline;
    } else if (end >= length) {
        end = length;
    } else {
        int space = end;
        while (space > from && text[space] != ' ') space--;
        if (space > from) end = space;
    }
    int written = 0;
    for (int i = from; i < end; i++) out[written++] = text[i];
    out[written] = 0;
    return text[end] == ' ' || text[end] == '\n' ? end + 1 : end;
}

}  // namespace

void draw_confirm(ui::Canvas& canvas, const ConfirmSnapshot& snapshot) {
    canvas.clear(true);

    // Reversed out across the full width: whatever page was up, this cannot be
    // mistaken for one of them at arm's length.
    canvas.rect(0, 0, kGlassWidth, 26, true, /*fill=*/true);
    canvas.draw_text(kLeft + kCellW, kConfirmHeaderY, "AUTHORISE", false, 2);

    canvas.draw_text(kLeft, kConfirmTitleY, snapshot.title, true, 2);
    canvas.hline(kLeft, kConfirmTitleY + 22, kGlassWidth - 2 * kLeft, true);

    char line[kConfirmDetailCols + 1];
    int at = 0;
    for (int row = 0; row < kConfirmDetailRows; row++) {
        at = take_line(snapshot.detail, at, line);
        if (line[0] == 0) break;
        canvas.draw_text(kLeft, confirm_detail_y(row), line, true, 1);
    }

    // The allowing gesture is reversed out and the refusing one is not, so the
    // two are told apart by shape before either is read.
    const int w = (static_cast<int>(std::strlen(kConfirmAllowText)) + 2) * kCellW;
    canvas.rect(kLeft, kConfirmAllowY - 4, w, 15, true, /*fill=*/true);
    canvas.draw_text(kLeft + kCellW, kConfirmAllowY, kConfirmAllowText, false, 1);
    canvas.draw_text(kLeft + kCellW, kConfirmRefuseY, kConfirmRefuseText, true, 1);

    if (snapshot.timeout_s == 0) return;
    char text[32];
    int written = format_string(text, "REFUSED AFTER ");
    written += format_uint(text + written, snapshot.timeout_s);
    written += format_string(text + written, " S");
    text[written] = 0;
    canvas.draw_text(kLeft, kConfirmExpiryY, text, true, 1);
}

}  // namespace skyblip::go
