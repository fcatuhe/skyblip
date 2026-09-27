#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_STATUS_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_STATUS_H

#include <cstdint>

#include "core/gnss/acquisition.h"
#include "core/power/charging.h"
#include "products/skyblip_go/glass.h"

namespace skyblip::go {

struct StatusSnapshot {
    uint32_t device_addr{0};
    // INFO: fc 20sep26 the only place a pilot reads back the name payload 66 puts on air
    const char* callsign{""};
    bool fix_valid{false};
    bool utc_valid{false};
    bool transmitting{false};
    bool baro_valid{false};
    uint8_t sats{0};
    gnss::Stage stage{gnss::Stage::Silent};
    uint32_t stage_s{0};
    uint8_t fix_mode{0};
    int32_t lat_1e7{0};
    int32_t lon_1e7{0};
    int32_t alt_mm{0};      // GNSS, WGS-84 ellipsoid
    int32_t alt_std_mm{0};  // pressure altitude, 1013.25 hPa datum (QNE)
    uint32_t pressure_mpa{0};
    int32_t speed_mm_s{0};  // as measured, not as transmitted
    int32_t track_cdeg{0};  // hundredths of a degree
    int32_t climb_mm_s{0};  // as measured, not as transmitted
    uint32_t utc{0};
    int n_targets{0};
    const char* imu_stage{"NONE"};
    const char* imu_fault{""};
    uint32_t imu_fifo_bytes{0};
    uint32_t imu_unparsed{0};
    uint8_t imu_error{0};
    uint8_t imu_interrupt{0};
    uint8_t imu_meta{0};
    uint8_t imu_sensor_error{0};
    uint8_t imu_errored_sensor{0};
    bool slip_valid{false};
    int16_t slip_mg{0};
    bool battery_valid{false};
    bool charging{false};
    bool battery_low{false};
    power::ChargeCondition charge{power::ChargeCondition::Unknown};
    uint16_t battery_mv{0};
    uint8_t battery_percent{0};
};

void draw_status(ui::Canvas& fb, const StatusSnapshot& snap);

}  // namespace skyblip::go

#endif
