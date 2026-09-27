#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_RADAR_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_RADAR_H

#include <cstdint>

#include "core/gnss/acquisition.h"
#include "core/traffic/alarm.h"
#include "core/units/units.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/page.h"
#include "products/skyblip_go/settings.h"

namespace skyblip::go {

using skyblip::kMetresPerNm;
constexpr int kMaxRadarTargets = 12;

struct RadarTarget {
    int32_t north_m;
    int32_t east_m;
    int32_t up_m;
    traffic::Level alarm_level{traffic::Level::None};
    int16_t climb_e8{0};
    bool climb_valid{false};
    int32_t speed_mm_s{0};
    int32_t track_cdeg{0};
    int16_t turn_cdps{0};
    bool turn_valid{false};
    bool in_formation{false};
    bool alarm_dismissed{false};
};

struct RadarSnapshot {
    bool fix_valid{false};
    gnss::Stage stage{gnss::Stage::Silent};
    go::Units units{go::Units::Nautical};
    int range_step{kDefaultRangeStep};
    RadarPlot plot{RadarPlot::All};
    int32_t track_cdeg{0};
    int16_t turn_cdps{0};
    int32_t speed_mm_s{0};
    uint32_t flight_seconds{0};
    bool flight_time_valid{false};
    bool in_flight{false};
    bool taxiing{false};
    bool receiver_listening{false};
    int heard{0};
    int n_targets{0};
    const RadarTarget* targets{nullptr};
    bool alarm_flash{false};
    int formation_members{0};
    uint8_t battery_percent{0};
    bool battery_low{false};
};

void draw_radar(ui::Canvas& fb, const RadarSnapshot& snap);

}

#endif
