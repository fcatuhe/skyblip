#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_NEARBY_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_NEARBY_H

#include <cstdint>

#include "core/traffic/range.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/settings.h"

namespace skyblip::go {

constexpr int kNearbyRows = 8;

struct NearbySnapshot {
    settings::AirAddress own_addr{};
    const char* own_callsign{nullptr};
    bool fix_valid{false};
    go::Units units{go::Units::Nautical};
    int n_heard{0};
    int n_rows{0};
    const traffic::RangeRow* rows{nullptr};
};

constexpr int kNearbyScale = 2;
constexpr int kNearbyTitleScale = 2;
constexpr int kNearbyWordGap = 3;
constexpr int kNearbyCellW = 6 * kNearbyScale;
constexpr int kNearbyIdX = 4;
constexpr int kNearbyIdChars = 8;
constexpr int kNearbyOwnNameChars = 6;
constexpr int kNearbyDistEnd = 152;
constexpr int kNearbyRelEnd = 196;
constexpr int kNearbyTitleY = 2;
constexpr int kNearbyHeadY = 22;
constexpr int kNearbyFirstRowY = 37;
constexpr int kNearbyLineH = 20;
constexpr int kNearbyBannerY = kNearbyFirstRowY + kNearbyLineH;

void draw_nearby(ui::Canvas& fb, const NearbySnapshot& snap);

}  // namespace skyblip::go

#endif
