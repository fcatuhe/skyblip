#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_NEARBY_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_NEARBY_H

#include <cstdint>

#include "core/traffic/range.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/settings.h"

namespace skyblip::go {

constexpr int kNearbyRows = 8;

struct NearbySnapshot {
    settings::AirAddress own_address{};
    const char* own_callsign{nullptr};
    bool fix_valid{false};
    go::Units units{go::Units::Nautical};
    int heard_count{0};
    int row_count{0};
    const traffic::RangeRow* rows{nullptr};
};

constexpr int kNearbyScale = 2;
constexpr int kNearbyTitleScale = 2;
constexpr int kNearbyWordGap = 3;
constexpr int kNearbyCellWidth = ui::kCellWidth * kNearbyScale;
constexpr int kNearbyIdX = 4;
constexpr int kNearbyIdCharacters = 8;
constexpr int kNearbyOwnNameCharacters = 6;
constexpr int kNearbyDistanceEnd = 152;
constexpr int kNearbyRelativeAltitudeEnd = 196;
constexpr int kNearbyTitleY = 2;
constexpr int kNearbyHeadY = 22;
constexpr int kNearbyFirstRowY = 37;
constexpr int kNearbyLineHeight = 20;
constexpr int kNearbyBannerY = kNearbyFirstRowY + kNearbyLineHeight;

void draw_nearby(ui::Canvas& canvas, const NearbySnapshot& snapshot);

}  // namespace skyblip::go

#endif
