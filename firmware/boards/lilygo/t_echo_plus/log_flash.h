#ifndef SKYBLIP_BOARDS_T_ECHO_PLUS_LOG_FLASH_H
#define SKYBLIP_BOARDS_T_ECHO_PLUS_LOG_FLASH_H

#include <cstdint>

#if defined(CONFIG_BOARD_T_ECHO_PLUS)
#include <zephyr/devicetree.h>
#endif

namespace skyblip::boards::t_echo_plus {

constexpr uint32_t kLogSectorBytes = 4096;
constexpr uint32_t kLogSectors = 298;

#if defined(CONFIG_BOARD_T_ECHO_PLUS)
static_assert(DT_REG_SIZE(DT_NODELABEL(log_partition)) == kLogSectors * kLogSectorBytes);
#endif

}  // namespace skyblip::boards::t_echo_plus

#endif
