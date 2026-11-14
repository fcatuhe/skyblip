#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_ACTIVATION_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_ACTIVATION_H

#include <cstdint>

#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/frame.h"

namespace skyblip::go {

constexpr const char* kActivationTitle = "ACTIVATE";
constexpr const char* kActivationWhere = "GO TO SKYBLIP.EU";
constexpr const char* kActivationBody[] = {kActivationWhere, "/ACTIVATE.", "", "THEN SELECT:"};
constexpr int kActivationBodyRows = 4;
constexpr int kActivationTagY = 120;
constexpr int kActivationTagScale = 4;

void draw_activation(ui::Canvas& canvas, uint16_t bluetooth_tag);

}  // namespace skyblip::go

#endif
