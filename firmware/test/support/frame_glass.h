// Harness, not a test: the heading, the centred lines and the two keys, read back off the glass.
#ifndef SKYBLIP_TEST_SUPPORT_FRAME_GLASS_H
#define SKYBLIP_TEST_SUPPORT_FRAME_GLASS_H

#include "products/skyblip_go/pages/frame.h"
#include "test/support/glass_text.h"

namespace skyblip {

inline bool reads_heading(const ui::Canvas& canvas, const char* title, go::Heading heading) {
    return reads_at(canvas, go::heading_title_x(title, heading), go::heading_title_y(heading),
                    title, go::kFrameScale);
}

inline bool reads_centred(const ui::Canvas& canvas, int y, const char* text,
                          int scale = go::kFrameScale) {
    return reads_at(canvas, go::centred_x(text, scale), y, text, scale);
}

inline bool fits_a_line(const char* text) { return go::text_cells(text) <= go::kFrameColumns; }

inline bool reads_centred_lines(const ui::Canvas& canvas, int y, const char* const* lines,
                                int count) {
    for (int i = 0; i < count; i++) {
        if (lines[i][0] == 0) {
            y += go::kParagraphGap;
            continue;
        }
        if (!fits_a_line(lines[i]) || !reads_centred(canvas, y, lines[i])) return false;
        y += go::line_pitch(go::kFrameScale);
    }
    return true;
}

inline bool reads_button_key(const ui::Canvas& canvas, const char* verb) {
    return reads_in(canvas, verb, go::kButtonKeyX, go::kKeyTop, go::kButtonKeyX + go::kKeyWidth,
                    go::kKeyTop + go::kKeyHeight, go::kFrameScale, /*ink=*/false);
}

inline bool reads_pad_key(const ui::Canvas& canvas, const char* verb) {
    return reads_in(canvas, verb, go::kPadKeyX, go::kKeyTop, go::kPadKeyX + go::kKeyWidth,
                    go::kKeyTop + go::kKeyHeight, go::kFrameScale);
}

inline int key_ink(const ui::Canvas& canvas, int x) {
    int ink = 0;
    for (int y = go::kKeyTop; y < go::kKeyTop + go::kKeyHeight; y++)
        for (int dx = 0; dx < go::kKeyWidth; dx++) ink += canvas.get_pixel(x + dx, y) ? 1 : 0;
    return ink;
}

inline bool has_button_key(const ui::Canvas& canvas) {
    return key_ink(canvas, go::kButtonKeyX) > 0;
}

inline bool has_pad_key(const ui::Canvas& canvas) { return key_ink(canvas, go::kPadKeyX) > 0; }

}  // namespace skyblip

#endif
