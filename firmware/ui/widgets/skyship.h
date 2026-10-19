#ifndef SKYBLIP_UI_WIDGETS_SKYSHIP_H
#define SKYBLIP_UI_WIDGETS_SKYSHIP_H

#include "ui/canvas.h"

namespace skyblip::ui {

constexpr int kSkyshipRowsToNose = 5;
constexpr int kSkyshipSpan = 24;
constexpr int kSkyshipRows = 16;

// skyShip, the ownship symbol, with its hot spot on (cx, cy). Every screen that
// says "this is you" draws this one, which is why it is a widget and not part of
// whichever screen happened to need it first.
void draw_skyship(Canvas& canvas, int centre_x, int centre_y);

}  // namespace skyblip::ui

#endif
