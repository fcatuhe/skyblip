#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_RADAR_TRAFFIC_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_RADAR_TRAFFIC_H

#include <cstdint>

#include "products/skyblip_go/pages/radar.h"

namespace skyblip::go::radar {

struct Rect {
    int left;
    int top;
    int right;
    int bottom;
};

void plot_leaders(ui::Canvas& fb, const RadarSnapshot& snap, int16_t track);
void plot_blips(ui::Canvas& fb, const RadarSnapshot& snap, int16_t track);
void plot_rim(ui::Canvas& fb, const RadarSnapshot& snap, int16_t track, const Rect& label);

}  // namespace skyblip::go::radar

#endif
