#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_GMETER_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_GMETER_H

#include <cstdint>

#include "core/flight/gload.h"
#include "products/skyblip_go/glass.h"

namespace skyblip::go {

struct GMeterSnapshot {
    bool fitted{false};
    bool valid{false};
    flight::GLoad now{};
    flight::GLoad most{};
    flight::GLoad least{};
};

void draw_gmeter(ui::Canvas& canvas, const GMeterSnapshot& snapshot);

}  // namespace skyblip::go

#endif
