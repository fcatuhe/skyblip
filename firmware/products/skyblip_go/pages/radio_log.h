#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_RADIO_LOG_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_RADIO_LOG_H

#include <cstdint>

#include "core/radio/log.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/page.h"

namespace skyblip::go {

constexpr int kRadioLogRows = radio::Log::kCapacity;

enum class PpsState : uint8_t { None, Lock, Holdover };

struct GnssReception {
    bool fix_valid{false};
    uint8_t sats{0};
    PpsState pps{PpsState::None};
    uint16_t pps_age_s{0};
};

struct RadioLogSnapshot {
    GnssReception gnss{};
    uint32_t rx_ok{0};
    uint32_t tx_ok{0};
    uint32_t rx_noise{0};
    int8_t noise_dbm{0};
    int row_count{0};
    const radio::Log* log{nullptr};
};

void draw_radio_log(ui::Canvas& canvas, const RadioLogSnapshot& snapshot);

}  // namespace skyblip::go

#endif
