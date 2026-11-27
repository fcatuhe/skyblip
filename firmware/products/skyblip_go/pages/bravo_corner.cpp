#include "products/skyblip_go/pages/bravo_corner.h"

#include <algorithm>

#include "core/util/format.h"
#include "products/skyblip_go/pages/bravo_word.h"
#include "products/skyblip_go/pages/frame.h"

namespace skyblip::go {

int format_cell(char* out, std::optional<uint8_t> percent) {
    int written = format_string(out, "BAT ");
    if (percent) {
        written += format_unsigned(out + written, *percent);
        written += format_string(out + written, "%");
    } else {
        written += format_string(out + written, "LOW");
    }
    out[written] = 0;
    return written;
}

// INFO: fc 17nov26 Bravo's lamp winks red from its knee, and in a bag nobody sees it
CornerPatch draw_bravo_corner(ui::Canvas& canvas, const BravoCorner& corner, MarkLines marks) {
    if (corner.state == comms::BravoState::Unpaired) return {};
    char cell[12];
    const char* lines[kBravoCornerLines] = {"BRAVO"};
    int count = 1;
    if (corner.cell_low) {
        format_cell(cell, corner.cell_percent);
        lines[count++] = cell;
    }
    if (const char* word = bravo_word(corner.state)) lines[count++] = word;
    if (count == 1) return {};
    if (marks == MarkLines::WordOnly) count = 1;
    int width = 0;
    for (int i = 0; i < count; i++) width = std::max(width, text_width(lines[i], 1));
    const int right = kGlassWidth - kBravoCornerMargin;
    const int pitch = ui::kCharacterHeight + kBravoCornerStackGap;
    const CornerPatch patch{right - width - kBravoCornerPad, kBravoCornerMargin - kBravoCornerPad,
                            width + 2 * kBravoCornerPad,
                            count * pitch - kBravoCornerStackGap + 2 * kBravoCornerPad};
    canvas.rectangle(patch.x, patch.y, patch.width, patch.height, false, true);
    for (int i = 0; i < count; i++)
        canvas.draw_text(right - text_width(lines[i], 1), kBravoCornerMargin + i * pitch, lines[i],
                         true, 1);
    return patch;
}

void reverse_bravo_corner(ui::Canvas& canvas, const CornerPatch& patch) {
    for (int y = patch.y; y < patch.y + patch.height; y++)
        for (int x = patch.x; x < patch.x + patch.width; x++)
            canvas.set_pixel(x, y, !canvas.get_pixel(x, y));
}

void draw_bravo_corner_over(ui::Canvas& canvas, const BravoCorner& corner, MarkLines marks) {
    const CornerPatch patch = draw_bravo_corner(canvas, corner, marks);
    if (comms::lost(corner.state)) reverse_bravo_corner(canvas, patch);
}

}  // namespace skyblip::go
