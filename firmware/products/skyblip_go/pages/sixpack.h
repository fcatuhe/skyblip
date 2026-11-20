#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_SIXPACK_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_SIXPACK_H

#include <cstdint>

#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/settings.h"

namespace skyblip::go {

struct SixPackSnapshot {
    bool fix_valid{false};
    go::Units units{go::Units::Nautical};
    int32_t speed_kt{0};
    int32_t altitude_ft{0};
    int32_t climb_fpm{0};
    bool climb_valid{false};
    uint16_t track_deg{0};
    // INFO: fc 20nov26 positive turning right
    int16_t turn_cdps{0};
    uint8_t battery_percent{0};
    bool battery_valid{false};
    bool charging{false};
    uint16_t battery_mv{0};
    // INFO: fc 20nov26 positive to the right, across the wings
    int16_t lateral_mg{0};
    bool inclinometer_fitted{false};
    bool lateral_valid{false};
};

void draw_sixpack(ui::Canvas& canvas, const SixPackSnapshot& snapshot);

}  // namespace skyblip::go

#endif
