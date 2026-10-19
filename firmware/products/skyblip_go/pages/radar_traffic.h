#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_RADAR_TRAFFIC_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_RADAR_TRAFFIC_H

#include <cstdint>

#include "products/skyblip_go/pages/radar.h"

namespace skyblip::go::radar {

struct Bounds {
    int left;
    int top;
    int right;
    int bottom;
};

void plot_leaders(ui::Canvas& canvas, const RadarSnapshot& snapshot, int16_t track_angle16);
void plot_blips(ui::Canvas& canvas, const RadarSnapshot& snapshot, int16_t track_angle16);
void plot_rim(ui::Canvas& canvas, const RadarSnapshot& snapshot, int16_t track_angle16,
              const Bounds& label);

}  // namespace skyblip::go::radar

#endif
