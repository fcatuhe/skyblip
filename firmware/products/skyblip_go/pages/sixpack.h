#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_SIXPACK_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_SIXPACK_H

#include <cstdint>

#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/settings.h"

namespace skyblip::go {

struct SixPackSnapshot {
    bool data_valid{false};
    go::Units units{go::Units::Nautical};
    int32_t speed_kt{0};
    int32_t alt_ft{0};
    int32_t vs_fpm{0};
    bool vs_valid{false};
    uint16_t track_deg{0};
    int16_t turn_cdps{0};  // rate of turn in hundredths of a degree a second, positive = right
    uint8_t battery_percent{0};
    bool battery_valid{false};
    bool charging{false};
    uint16_t battery_mv{0};
    int16_t lateral_mg{0};  // right-positive, thousandths of g across the wings
    bool inclinometer_fitted{false};
    bool lateral_valid{false};
};

void draw_sixpack(ui::Canvas& fb, const SixPackSnapshot& snap);

}  // namespace skyblip::go

#endif
