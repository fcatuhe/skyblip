#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_NEARBY_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_NEARBY_H

#include <cstdint>
#include <optional>

#include "core/comms/peer.h"
#include "core/gnss/acquisition.h"
#include "core/traffic/range.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/settings.h"

namespace skyblip::go {

constexpr int kNearbyRows = 8;

struct NearbyGo {
    std::optional<uint8_t> battery_percent{};
    bool charging{false};
    gnss::Stage stage{gnss::Stage::Silent};
    uint8_t satellites{0};
    int traffic{0};
};

struct NearbyBravo {
    comms::BravoState state{comms::BravoState::Unpaired};
    std::optional<uint8_t> battery_percent{};
    bool charging{false};
    std::optional<uint8_t> satellites{};
    int traffic{0};
};

struct NearbySnapshot {
    NearbyGo go{};
    NearbyBravo bravo{};
    bool fix_valid{false};
    bool bravo_fix{false};
    go::Units units{go::Units::Nautical};
    int row_count{0};
    const traffic::RangeRow* rows{nullptr};
};

constexpr int kNearbyScale = 2;
constexpr int kNearbyCellWidth = ui::kCellWidth * kNearbyScale;
constexpr int kNearbyIdX = 4;
constexpr int kNearbyIdCharacters = 8;
constexpr int kNearbyAddressX = kNearbyIdX + 2 * kNearbyCellWidth;
// INFO: fc 18nov26 the small font is 7 rows on a 14-row line, so it cannot centre closer than this
constexpr int kNearbyAddressDrop = 4;
constexpr int kNearbyBatteryEnd = 82;
constexpr int kNearbySatellitesEnd = 142;
constexpr int kNearbyDistanceEnd = 152;
constexpr int kNearbyRelativeAltitudeEnd = 196;
constexpr int kNearbyWordCharacters = 9;
constexpr int kNearbyUnitY = 2;
constexpr int kNearbyLineHeight = 20;
constexpr int kNearbyHeadY = kNearbyUnitY + kNearbyLineHeight;
constexpr int kNearbyFirstRowY = 37;
constexpr int kNearbyBannerY = kNearbyFirstRowY + kNearbyLineHeight;

static_assert(kNearbyRelativeAltitudeEnd - kNearbyWordCharacters * kNearbyCellWidth >
                  kNearbyBatteryEnd,
              "the widest word a unit row says clears its battery");

constexpr int nearby_shift(bool paired) { return paired ? kNearbyLineHeight : 0; }

void draw_nearby(ui::Canvas& canvas, const NearbySnapshot& snapshot);

}  // namespace skyblip::go

#endif
