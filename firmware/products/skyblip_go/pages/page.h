#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_PAGE_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_PAGE_H

#include <cstdint>

#include "core/units/units.h"
#include "core/util/intmath.h"
#include "products/skyblip_go/settings.h"

namespace skyblip::go {

enum class Page : uint8_t {
    Radar = 0,
    Nearby = 1,
    SixPack = 2,
    GMeter = 3,
    Status = 4,
    Sats = 5,
    RadioLog = 6,
    Raw = 7,
    Capture = 8,
    SelfTest = 9,
    kCount = 10
};

constexpr int kPageCount = static_cast<int>(Page::kCount);

constexpr int32_t kRangeStepsNm[kRangeStepCount] = {1, 2, 4, 8};
constexpr int32_t kRangeStepsKm[kRangeStepCount] = {2, 4, 8, 16};
constexpr int32_t kMetresPerKm = 1000;

constexpr int clamped_range_step(int step) {
    return step >= 0 && step < kRangeStepCount ? step : kDefaultRangeStep;
}

constexpr int next_range_step(int step) { return (clamped_range_step(step) + 1) % kRangeStepCount; }

constexpr int32_t range_value(int step, Units units) {
    return units == Units::Metric ? kRangeStepsKm[clamped_range_step(step)]
                                  : kRangeStepsNm[clamped_range_step(step)];
}

constexpr int64_t range_metres(int step, Units units) {
    return static_cast<int64_t>(range_value(step, units)) *
           (units == Units::Metric ? kMetresPerKm : kMetresPerNm);
}

constexpr const char* range_unit(Units units) { return units == Units::Metric ? "KM" : "NM"; }

constexpr int32_t kClimbFigureStepFpm = 10;

constexpr int32_t climb_figure_fpm(int32_t fpm) {
    return div_round(fpm, kClimbFigureStepFpm) * kClimbFigureStepFpm;
}

constexpr RadarPlot next_radar_plot(RadarPlot plot) {
    return plot == RadarPlot::All ? RadarPlot::ToScale : RadarPlot::All;
}

// INFO: fc 20sep26 four digits of seconds, so a clock with no UTC behind it still fits its column
constexpr uint32_t kUptimeClockWrapS = 10000;

constexpr int kWalkedPages = 4;

constexpr bool walked(Page page) { return static_cast<int>(page) < kWalkedPages; }

constexpr bool diagnostic(Page page) { return !walked(page); }

constexpr bool diagnostics_menu(Page owner) { return owner == Page::Nearby; }

constexpr const char* page_title(Page page) {
    switch (page) {
        case Page::Radar: return "RADAR";
        case Page::Nearby: return "NEARBY";
        case Page::SixPack: return "SIX-PACK";
        case Page::Status: return "STATUS";
        case Page::Sats: return "SATELLITES";
        case Page::RadioLog: return "RADIO LOG";
        case Page::Raw: return "RAW";
        case Page::Capture: return "CAPTURE";
        case Page::SelfTest: return "SELF TEST";
        case Page::GMeter: return "G METER";
        default: return "";
    }
}

constexpr const char* menu_title(Page page) {
    switch (page) {
        case Page::Radar: return "SETTINGS";
        default: return diagnostics_menu(page) ? "DIAGNOSTICS" : page_title(page);
    }
}

constexpr Page menu_owner(Page page) { return walked(page) ? page : Page::Nearby; }

// INFO: fc 18sep26 a page off the walk was opened from a menu, so the pad hands it back there
constexpr Page page_after(Page page) {
    return walked(page) ? static_cast<Page>((static_cast<int>(page) + 1) % kWalkedPages)
                        : menu_owner(page);
}

}  // namespace skyblip::go

#endif
