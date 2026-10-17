#include <cstdio>

#include "ui/canvas.h"
#include "ui/widgets/blip.h"

using skyblip::ui::Trend;
using skyblip::ui::Vertical;

namespace {

constexpr int kW = 19;
constexpr int kH = 25;
constexpr int kX = kW / 2;
constexpr int kY = kH / 2;

struct Band {
    const char* name;
    Vertical vertical;
    bool alarms;
};

constexpr Band kBands[] = {
    {"above", Vertical::Above, true},
    {"level", Vertical::Level, true},
    {"below", Vertical::Below, true},
    {"far-above", Vertical::FarAbove, false},
    {"far-below", Vertical::FarBelow, false},
};

struct Motion {
    const char* name;
    Trend trend;
};

constexpr Motion kMotions[] = {
    {"climbing", Trend::Climbing},
    {"steady", Trend::Steady},
    {"sinking", Trend::Sinking},
};

void emit(const char* name, Vertical vertical, Trend trend, bool alarm) {
    skyblip::ui::Panel<kW, kH> panel;
    panel.clear();
    skyblip::ui::draw_blip(panel, kX, kY, vertical, trend, alarm);

    std::printf("%s %d %d\n", name, kW, kH);
    for (int y = 0; y < kH; y++) {
        for (int x = 0; x < kW; x++) std::putchar(panel.get_pixel(x, y) ? '1' : '0');
        std::putchar('\n');
    }
}

}  // namespace

int main() {
    char name[64];
    for (const Band& band : kBands)
        for (const Motion& motion : kMotions) {
            std::snprintf(name, sizeof(name), "%s-%s", band.name, motion.name);
            emit(name, band.vertical, motion.trend, false);
            if (!band.alarms) continue;
            std::snprintf(name, sizeof(name), "%s-%s-alarm", band.name, motion.name);
            emit(name, band.vertical, motion.trend, true);
        }
    return 0;
}
