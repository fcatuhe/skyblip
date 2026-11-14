#include "ui/widgets/mark.h"

#include <cstdint>
#include <initializer_list>

namespace skyblip::ui {

namespace {

// INFO: fc 14nov26 hundredths of a pixel, from server/public/icon.svg: dot 6, arcs 13 and 20.5,
// stroke 3.5
struct Geometry {
    int32_t dot;
    int32_t inner;
    int32_t outer;
    int32_t half_stroke;
};

constexpr Geometry geometry(MarkSize size) {
    return size == MarkSize::Small ? Geometry{252, 546, 861, 74} : Geometry{480, 1040, 1640, 140};
}

constexpr int32_t kTan46DegreesThousandths = 1036;
constexpr int kReach = 24;

bool inked(const Geometry& g, int x, int y) {
    const int32_t dx = 100 * x + 50;
    const int32_t dy = 100 * y + 50;
    const int32_t d2 = dx * dx + dy * dy;
    if (d2 <= g.dot * g.dot) return true;
    const int32_t up = -dy;
    if (up <= 0 || (dx < 0 ? -dx : dx) * 1000 > kTan46DegreesThousandths * up) return false;
    for (const int32_t radius : {g.inner, g.outer}) {
        const int32_t near = radius - g.half_stroke;
        const int32_t far = radius + g.half_stroke;
        if (d2 >= near * near && d2 <= far * far) return true;
    }
    return false;
}

struct Box {
    int left, top, right, bottom;
};

Box ink_box(const Geometry& g) {
    Box box{kReach, kReach, -kReach, -kReach};
    for (int y = -kReach; y <= kReach; y++)
        for (int x = -kReach; x <= kReach; x++)
            if (inked(g, x, y)) {
                if (x < box.left) box.left = x;
                if (x > box.right) box.right = x;
                if (y < box.top) box.top = y;
                if (y > box.bottom) box.bottom = y;
            }
    return box;
}

}  // namespace

MarkExtent mark_extent(MarkSize size) {
    const Box box = ink_box(geometry(size));
    return {box.right - box.left + 1, box.bottom - box.top + 1};
}

void draw_mark(Canvas& canvas, int ink_left, int ink_top, MarkSize size, bool black) {
    const Geometry g = geometry(size);
    const Box box = ink_box(g);
    for (int y = box.top; y <= box.bottom; y++)
        for (int x = box.left; x <= box.right; x++)
            if (inked(g, x, y))
                canvas.set_pixel(ink_left + x - box.left, ink_top + y - box.top, black);
}

}  // namespace skyblip::ui
