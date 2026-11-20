#ifndef SKYBLIP_UI_WIDGETS_SKYSHIP_H
#define SKYBLIP_UI_WIDGETS_SKYSHIP_H

#include "ui/canvas.h"

namespace skyblip::ui {

constexpr int kSkyshipRowsToNose = 5;
constexpr int kSkyshipSpan = 24;
constexpr int kSkyshipRows = 16;

// INFO: fc 20nov26 the hot spot row, not the sprite's centre, lands on (centre_x, centre_y)
void draw_skyship(Canvas& canvas, int centre_x, int centre_y);

}  // namespace skyblip::ui

#endif
