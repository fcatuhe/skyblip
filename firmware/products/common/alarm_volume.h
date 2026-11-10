#ifndef SKYBLIP_PRODUCTS_COMMON_ALARM_VOLUME_H
#define SKYBLIP_PRODUCTS_COMMON_ALARM_VOLUME_H

#include <cstdint>

namespace skyblip::common {

enum class AlarmVolume : uint8_t { Off = 0, Soft = 1, Loud = 2 };

}  // namespace skyblip::common

#endif
