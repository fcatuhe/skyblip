#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_SATELLITES_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_SATELLITES_H

#include <cstdint>

#include "core/gnss/acquisition.h"
#include "core/gnss/sky.h"
#include "ports/gnss.h"
#include "products/skyblip_go/glass.h"

namespace skyblip::go {

struct SatellitesSnapshot {
    bool fix_valid{false};
    bool levels_live{false};
    gnss::Stage stage{gnss::Stage::Silent};
    uint32_t stage_s{0};
    uint8_t satellites{0};
    uint16_t hdop_e2{0};
    uint16_t vdop_e2{0};
    // INFO: fc 20sep26 how far into its own second the last solution landed, 450 ms costs bursts
    uint16_t nav_ms{0};
    bool nav_valid{false};
    ports::GnssHealth health{};
    const gnss::SkyView* sky{nullptr};
};

void draw_satellites(ui::Canvas& canvas, const SatellitesSnapshot& snapshot);

}  // namespace skyblip::go

#endif
