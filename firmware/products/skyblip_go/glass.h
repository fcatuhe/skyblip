#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_GLASS_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_GLASS_H

#include "boards/lilygo/t_echo_plus/glass.h"
#include "ui/canvas.h"

namespace skyblip::go {

constexpr int kGlassWidth = boards::t_echo_plus::kGlassWidth;
constexpr int kGlassHeight = boards::t_echo_plus::kGlassHeight;

using Glass = ui::Panel<kGlassWidth, kGlassHeight>;

}  // namespace skyblip::go

#endif
