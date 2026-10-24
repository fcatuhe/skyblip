#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_RADAR_GEOMETRY_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_RADAR_GEOMETRY_H

#include <cstdint>

#include "core/util/intmath.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/page.h"

namespace skyblip::go::radar {

// The screen is 200x200, an EVEN grid: there is no middle pixel. The centre is
// the POINT where four pixels meet, so each axis has a near-side and a far-side
// middle pixel - 99 and 100. Everything on this screen is built around that
// point rather than around a pixel:
//
//   kNear = 99   the pixel just before the centre (left, and above)
//   kFar  = 100  the pixel just after it (right, and below)
//
// A feature at distance d from the centre therefore occupies kNear-(d-1) on one
// side and kFar+(d-1) on the other. A feature ON the centre is a PAIR of
// pixels, never one. That makes the own ship exactly centred (its fuselage
// straddles 99|100), the rings exactly concentric with it, and every target
// offset measured from the same point in both directions.
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
    const int64_t track_cosine = cosine(track_angle16), track_sine = sine(track_angle16);
    return {static_cast<int32_t>((north * track_cosine + east * track_sine) / kTrigOne),
            static_cast<int32_t>((east * track_cosine - north * track_sine) / kTrigOne)};
}

}  // namespace skyblip::go::radar

#endif
