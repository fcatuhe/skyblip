#ifndef SKYBLIP_BOARDS_T_ECHO_PLUS_IMU_MOUNT_H
#define SKYBLIP_BOARDS_T_ECHO_PLUS_IMU_MOUNT_H

#include <cstdint>

#include "core/events/sensor.h"
#include "hardware/parts/bhi260/bhi260.h"

namespace skyblip::boards::t_echo_plus {

// INFO: fc 19sep26 a bench read the g-meter inverted, so the chip is face down as well as turned
inline events::AccelerometerSample device_frame(const parts::Acceleration& chip, uint32_t now_ms) {
    return events::AccelerometerSample{chip.y_mg, chip.x_mg, static_cast<int16_t>(-chip.z_mg),
                                       now_ms};
}

}  // namespace skyblip::boards::t_echo_plus

#endif
