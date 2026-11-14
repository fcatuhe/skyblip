#ifndef SKYBLIP_UI_WIDGETS_MARK_H
#define SKYBLIP_UI_WIDGETS_MARK_H

#include "ui/canvas.h"

namespace skyblip::ui {

enum class MarkSize { Small, Large };

struct MarkExtent {
    int width;
    int height;
};

MarkExtent mark_extent(MarkSize size);

void draw_mark(Canvas& canvas, int ink_left, int ink_top, MarkSize size, bool black);

}  // namespace skyblip::ui

#endif
