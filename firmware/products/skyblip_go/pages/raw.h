#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_RAW_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_RAW_H

#include <cstdint>

#include "core/gnss/acquisition.h"
#include "core/timing/slot.h"
#include "ports/gnss.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/radio_log.h"

namespace skyblip::go {

struct RawGnss {
    ports::GnssHealth health{};
    gnss::Stage stage{gnss::Stage::Silent};
    PpsState pps{PpsState::None};
    uint32_t pps_age_ms{0};
    uint32_t solutions{0};
    uint32_t utc_s{0};
    uint16_t solution_phase_ms{0};
    uint16_t hdop_e2{0};
    uint16_t vdop_e2{0};
    uint16_t residual_m{0};
    uint8_t satellites{0};
    uint8_t in_use{0};
    uint8_t fix_mode{0};
    bool solution_phase_valid{false};
    bool residual_valid{false};
    bool fix_valid{false};
    bool utc_valid{false};
    bool settled{false};
    bool levels_live{false};
};

struct RawRadio {
    uint32_t rx_ok{0};
    uint32_t rx_bad{0};
    uint32_t rx_wait{0};
    uint32_t rx_type{0};
    uint32_t rx_unframed{0};
    uint32_t rx_miskeyed{0};
    uint32_t rx_noise{0};
    uint32_t rx_named{0};
    uint32_t uplink_frames{0};
    uint32_t uplink_bad{0};
    uint32_t uplink_targets{0};
    uint32_t tx_ok{0};
    uint32_t tx_lost{0};
    uint32_t tx_named{0};
    uint32_t missed{0};
    uint32_t refused{0};
    uint32_t duty_permille{0};
    uint32_t holdover{0};
    int32_t dwell_worst_us{0};
    int32_t pps_worst_us{0};
    uint16_t tx_keyed_us{0};
    uint16_t tx_span_us{0};
    uint16_t tracked{0};
    uint8_t alarm{0};
    int8_t noise_dbm{0};
    timing::SlotState slot{timing::SlotState::UplinkRxOBand};
    uint32_t frequency_hz{0};
    bool tx_allowed{false};
};

struct RawSnapshot {
    uint32_t uptime_s{0};
    RawGnss gnss{};
    RawRadio radio{};
};

void draw_raw(ui::Canvas& canvas, const RawSnapshot& snapshot);

}  // namespace skyblip::go

#endif
