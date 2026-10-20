// Harness, not a test: a case claims "it reads 047", not "there is ink up there".
#ifndef SKYBLIP_TEST_SUPPORT_GLASS_TEXT_H
#define SKYBLIP_TEST_SUPPORT_GLASS_TEXT_H

#include <cstring>

#include "products/skyblip_go/glass.h"
#include "ui/canvas.h"

namespace skyblip {

inline bool reads_in(const ui::Canvas& canvas, const char* text, int x0, int y0, int x1, int y1,
                     int scale = 1, bool ink = true) {
    const int characters = static_cast<int>(std::strlen(text));
    go::Glass wanted;
    wanted.clear(ink);
    wanted.draw_text(0, 0, text, ink, scale);
    const int width = characters * ui::kCellWidth * scale - scale,
              height = ui::kCharacterHeight * scale;
    for (int y = y0; y + height <= y1; y++) {
        for (int x = x0; x + width <= x1; x++) {
            bool same = true;
            for (int delta_y = 0; delta_y < height && same; delta_y++)
                for (int delta_x = 0; delta_x < width && same; delta_x++)
                    if (canvas.get_pixel(x + delta_x, y + delta_y) !=
                        wanted.get_pixel(delta_x, delta_y))
                        same = false;
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
    for (int delta_y = 0; delta_y < ui::kCharacterHeight * scale; delta_y++)
        for (int delta_x = 0; delta_x < width; delta_x++)
            if (canvas.get_pixel(x + delta_x, y + delta_y) !=
                expected.get_pixel(x + delta_x, y + delta_y))
                return false;
    return true;
}

}  // namespace skyblip

#endif
