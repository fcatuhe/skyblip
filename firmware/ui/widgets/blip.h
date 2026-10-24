#ifndef SKYBLIP_UI_WIDGETS_BLIP_H
#define SKYBLIP_UI_WIDGETS_BLIP_H

#include <cstdint>

#include "ui/canvas.h"

namespace skyblip::ui {

enum class Vertical : uint8_t { FarAbove, Above, Level, Below, FarBelow };
enum class Trend : uint8_t { Steady, Climbing, Sinking };

void draw_blip(Canvas& canvas, int x, int y, Vertical vertical, Trend trend, bool alarm);

int blip_above(Vertical vertical, Trend trend);
int blip_below(Vertical vertical, Trend trend);
int blip_beside(Vertical vertical);

}  // namespace skyblip::ui

#endif
