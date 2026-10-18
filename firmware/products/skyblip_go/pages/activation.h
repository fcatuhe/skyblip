#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_ACTIVATION_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_ACTIVATION_H

#include <cstdint>

#include "products/skyblip_go/glass.h"

namespace skyblip::go {

constexpr const char* kActivationHeader = "ACTIVATE";
constexpr const char* kActivationTitle = "NO RADIO YET";
constexpr const char* kActivationWhere = "AT SKYBLIP.EU/ACTIVATE";
constexpr int kActivationTagY = 136;
constexpr int kActivationTagScale = 4;
constexpr int kActivationNameY = 180;

void draw_activation(ui::Canvas& fb, uint16_t bluetooth_tag);

}  // namespace skyblip::go

#endif
