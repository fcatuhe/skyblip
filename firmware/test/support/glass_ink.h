// Harness, not a test: how much ink a box of the glass holds, where a case claims a mark.
#ifndef SKYBLIP_TEST_SUPPORT_GLASS_INK_H
#define SKYBLIP_TEST_SUPPORT_GLASS_INK_H

#include "products/skyblip_go/glass.h"

namespace skyblip {

inline int ink_in(const go::Glass& fb, int x0, int y0, int x1, int y1) {
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) n += fb.get_pixel(x, y) ? 1 : 0;
    return n;
}

}  // namespace skyblip

#endif
