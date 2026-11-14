#ifndef SKYBLIP_PROTOCOLS_COMMON_STATUS_H
#define SKYBLIP_PROTOCOLS_COMMON_STATUS_H

#include <cstdint>

namespace skyblip::wire {

enum class [[nodiscard]] Status : uint8_t { Ok, Invalid, Crc, Unsupported, Full };

}

#endif
