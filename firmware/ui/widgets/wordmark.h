#ifndef SKYBLIP_UI_WIDGETS_WORDMARK_H
#define SKYBLIP_UI_WIDGETS_WORDMARK_H

#include "ui/canvas.h"

namespace skyblip::ui {

// The skyBlip wordmark, centred on (centre_x, centre_y): the site's display face and the
// mark over the dotless i, baked to 1 bit by scripts/make_wordmark.py.
void draw_wordmark(Canvas& canvas, int centre_x, int centre_y);

int wordmark_width();
int wordmark_height();

}  // namespace skyblip::ui

#endif
