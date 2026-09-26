#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_RADAR_TRAFFIC_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_RADAR_TRAFFIC_H

#include <cstdint>

#include "products/skyblip_go/pages/radar.h"

namespace skyblip::go::radar {

int plot(ui::Canvas& fb, const RadarSnapshot& snap, int16_t track);

}  // namespace skyblip::go::radar

#endif
