// Harness, not a test: a case claims "it reads 047", not "there is ink up there".
#ifndef SKYBLIP_TEST_SUPPORT_GLASS_TEXT_H
#define SKYBLIP_TEST_SUPPORT_GLASS_TEXT_H

#include "products/skyblip_go/glass.h"

namespace skyblip {

inline bool reads_in(const ui::Canvas& fb, const char* text, int x0, int y0, int x1, int y1,
                     int scale = 1, bool ink = true) {
    int n = 0;
    while (text[n]) n++;
    go::Glass wanted;
    wanted.clear(ink);
    wanted.draw_text(0, 0, text, ink, scale);
    const int w = n * 6 * scale - scale, h = 7 * scale;
    for (int y = y0; y + h <= y1; y++) {
        for (int x = x0; x + w <= x1; x++) {
            bool same = true;
            for (int dy = 0; dy < h && same; dy++)
                for (int dx = 0; dx < w && same; dx++)
                    if (fb.get_pixel(x + dx, y + dy) != wanted.get_pixel(dx, dy)) same = false;
            if (same) return true;
        }
    }
    return false;
}

}  // namespace skyblip

#endif
