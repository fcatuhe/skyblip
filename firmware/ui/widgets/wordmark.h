#ifndef SKYBLIP_UI_WIDGETS_WORDMARK_H
#define SKYBLIP_UI_WIDGETS_WORDMARK_H

#include "ui/canvas.h"

namespace skyblip::ui {

void draw_wordmark(Canvas& canvas, int centre_x, int centre_y);

int wordmark_width();
int wordmark_height();

}  // namespace skyblip::ui

#endif
