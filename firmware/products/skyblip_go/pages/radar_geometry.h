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
constexpr int kNear = kGlassW / 2 - 1;
constexpr int kFar = kGlassW / 2;
constexpr int kMargin = 4;
constexpr int kToScaleR = 92;
constexpr int kAllR = 76;
constexpr int kRingW = 2;
constexpr int kRimR = kAllR + 11;

constexpr int ring_r(RadarPlot plot) { return plot == RadarPlot::All ? kAllR : kToScaleR; }
constexpr int kGlyphH = 7;
constexpr int kClockScale = 2;
constexpr int kStateScale = 1;
constexpr int kLabelPad = 2;
constexpr int kStackGap = 2;
constexpr int kFooterBottom = kGlassH - kMargin;
constexpr int kClockY = kFooterBottom - kGlyphH * kClockScale;
constexpr int kStateY = kClockY - kStackGap - kGlyphH * kStateScale;
constexpr int32_t kQ14One = 16384;
constexpr int kFooterTop = kStateY - kLabelPad;

struct HeadingUp {
    int32_t ahead;
    int32_t right;
};

inline HeadingUp heading_up(int32_t north, int32_t east, int16_t track) {
    const int64_t c = icos(track), s = isin(track);
    return {static_cast<int32_t>((north * c + east * s) / kQ14One),
            static_cast<int32_t>((east * c - north * s) / kQ14One)};
}

}  // namespace skyblip::go::radar

#endif
