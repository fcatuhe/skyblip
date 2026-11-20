#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_RADAR_GEOMETRY_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_RADAR_GEOMETRY_H

#include <cstdint>

#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/page.h"
#include "protocols/common/geometry.h"

namespace skyblip::go::radar {

// INFO: fc 20nov26 the centre is the point between 99 and 100, so a centred feature is a pixel pair
constexpr int kNear = kGlassWidth / 2 - 1;
constexpr int kFar = kGlassWidth / 2;
constexpr int kMargin = 4;
constexpr int kToScaleRadius = 92;
constexpr int kAllRadius = 76;
constexpr int kRingWidth = 2;
constexpr int kRimRadius = kAllRadius + 11;

constexpr int ring_radius(RadarPlot plot) {
    return plot == RadarPlot::All ? kAllRadius : kToScaleRadius;
}
using ui::kCharacterHeight;
constexpr int kClockScale = 2;
constexpr int kStateScale = 1;
constexpr int kLabelPad = 2;
constexpr int kStackGap = 2;
constexpr int kFooterBottom = kGlassHeight - kMargin;
constexpr int kClockY = kFooterBottom - kCharacterHeight * kClockScale;
constexpr int kStateY = kClockY - kStackGap - kCharacterHeight * kStateScale;
constexpr int kFooterTop = kStateY - kLabelPad;

struct HeadingUp {
    int32_t ahead;
    int32_t right;
};

inline HeadingUp heading_up(int32_t north, int32_t east, int16_t track_angle16) {
    const int64_t track_cosine = wire::cosine(track_angle16),
                  track_sine = wire::sine(track_angle16);
    return {static_cast<int32_t>((north * track_cosine + east * track_sine) / wire::kTrigOne),
            static_cast<int32_t>((east * track_cosine - north * track_sine) / wire::kTrigOne)};
}

}  // namespace skyblip::go::radar

#endif
