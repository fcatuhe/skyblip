#ifndef SKYBLIP_CORE_MODEL_BAND_H
#define SKYBLIP_CORE_MODEL_BAND_H

#include <cstdint>

namespace skyblip::model {
// INFO: fc 20nov26 here, not in core/timing, because a received burst carries its dwell's band
enum class Band : uint8_t { M = 0, O = 1 };

}  // namespace skyblip::model

#endif
