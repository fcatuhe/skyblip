// Harness, not a test: a case claims "it reads 047", not "there is ink up there".
#ifndef SKYBLIP_TEST_SUPPORT_GLASS_TEXT_H
#define SKYBLIP_TEST_SUPPORT_GLASS_TEXT_H

#include <cstring>

#include "products/skyblip_go/glass.h"
#include "ui/canvas.h"

namespace skyblip {

inline bool reads_in(const ui::Canvas& canvas, const char* text, int x0, int y0, int x1, int y1,
                     int scale = 1, bool ink = true) {
    const int n = static_cast<int>(std::strlen(text));
    go::Glass wanted;
    wanted.clear(ink);
    wanted.draw_text(0, 0, text, ink, scale);
    const int w = n * ui::kCellWidth * scale - scale, h = ui::kCharacterHeight * scale;
    for (int y = y0; y + h <= y1; y++) {
        for (int x = x0; x + w <= x1; x++) {
            bool same = true;
            for (int dy = 0; dy < h && same; dy++)
                for (int dx = 0; dx < w && same; dx++)
                    if (canvas.get_pixel(x + dx, y + dy) != wanted.get_pixel(dx, dy)) same = false;
            if (same) return true;
        }
    }
    return false;
}

// The same text drawn at the same place on a scratch panel, compared over the box it occupies.
inline bool reads_at(const ui::Canvas& canvas, int x, int y, const char* text, int scale = 1,
                     bool ink = true) {
    go::Glass expected;
    expected.clear(ink);
    expected.draw_text(x, y, text, ink, scale);
    const int width = static_cast<int>(std::strlen(text)) * ui::kCellWidth * scale;
    for (int dy = 0; dy < ui::kCharacterHeight * scale; dy++)
        for (int dx = 0; dx < width; dx++)
            if (canvas.get_pixel(x + dx, y + dy) != expected.get_pixel(x + dx, y + dy))
                return false;
    return true;
}

}  // namespace skyblip

#endif
