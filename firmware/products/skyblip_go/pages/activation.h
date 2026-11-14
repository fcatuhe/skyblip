#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_ACTIVATION_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_ACTIVATION_H

#include <cstdint>

#include "products/skyblip_go/glass.h"

namespace skyblip::go {

constexpr const char* kActivationHeader = "ACTIVATE";
constexpr const char* kActivationTitle = "NOT ACTIVATED";
constexpr const char* kActivationWhere = "GO TO SKYBLIP.EU/ACTIVATE.";
constexpr int kActivationTagY = 136;
constexpr int kActivationTagScale = 4;
constexpr int kActivationNameY = 180;

void draw_activation(ui::Canvas& canvas, uint16_t bluetooth_tag);

}  // namespace skyblip::go

#endif
