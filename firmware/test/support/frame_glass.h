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

inline bool reads_body(const ui::Canvas& canvas, go::Heading heading, bool keys,
                       const go::BodyLine* lines, int count) {
    int y = go::body_top(heading, keys, lines, count);
    const int rule_y = heading == go::Heading::Interrupt ? go::kInterruptRuleY : go::kOpenedRuleY;
    const int bottom = keys ? go::kKeyTop : go::kGlassHeight;
    // INFO: fc 14nov26 a body keeps glass between itself and the rule, and the keys or the foot
    if (y - rule_y <= go::kParagraphGap / 2 || bottom - (y + go::body_height(lines, count)) <= 2)
        return false;
    bool started = false;
    for (int i = 0; i < count; i++) {
        if (lines[i].text[0] == 0) {
            if (started) y += go::kParagraphGap;
            continue;
        }
        if (started) y += 2 * lines[i].scale;
        if (go::text_cells(lines[i].text) * ui::kCellWidth * lines[i].scale > go::kGlassWidth)
            return false;
        if (!reads_centred(canvas, y, lines[i].text, lines[i].scale)) return false;
        y += ui::kCharacterHeight * lines[i].scale;
        started = true;
    }
    return true;
}

inline bool reads_inside_body(const ui::Canvas& canvas, const char* text,
                              int scale = go::kFrameScale) {
    return reads_in(canvas, text, 0, go::kOpenedRuleY + 1, go::kGlassWidth, go::kKeyTop, scale);
}

inline bool reads_button_key(const ui::Canvas& canvas, const char* verb) {
    return reads_in(canvas, verb, go::kButtonKeyX, go::kKeyTop, go::kButtonKeyX + go::kKeyWidth,
                    go::kKeyTop + go::kKeyHeight, go::kFrameScale, /*ink=*/false);
}

inline bool reads_pad_key(const ui::Canvas& canvas, const char* verb) {
    return reads_in(canvas, verb, go::kPadKeyX, go::kKeyTop, go::kPadKeyX + go::kKeyWidth,
                    go::kKeyTop + go::kKeyHeight, go::kFrameScale);
}

// A key is known by its top edge, a stroke no line of text draws whole.
inline bool has_key_at(const ui::Canvas& canvas, int x) {
    constexpr int kPastCorner = 8;
    for (int dx = kPastCorner; dx < go::kKeyWidth - kPastCorner; dx++)
        if (!canvas.get_pixel(x + dx, go::kKeyTop)) return false;
    return true;
}

inline bool has_button_key(const ui::Canvas& canvas) { return has_key_at(canvas, go::kButtonKeyX); }

inline bool has_pad_key(const ui::Canvas& canvas) { return has_key_at(canvas, go::kPadKeyX); }

}  // namespace skyblip

#endif
