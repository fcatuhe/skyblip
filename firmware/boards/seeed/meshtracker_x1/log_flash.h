#ifndef SKYBLIP_BOARDS_MESHTRACKER_X1_LOG_FLASH_H
#define SKYBLIP_BOARDS_MESHTRACKER_X1_LOG_FLASH_H

#include <cstdint>

#if defined(CONFIG_BOARD_MESHTRACKER_X1)
#include <zephyr/devicetree.h>
#endif

namespace skyblip::boards::meshtracker_x1 {

constexpr uint32_t kLogSectorBytes = 4096;
constexpr uint32_t kLogSectors = 512;

#if defined(CONFIG_BOARD_MESHTRACKER_X1)
static_assert(DT_REG_SIZE(DT_NODELABEL(log_partition)) == kLogSectors * kLogSectorBytes);
#endif

}  // namespace skyblip::boards::meshtracker_x1

#endif
