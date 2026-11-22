#ifndef SKYBLIP_CORE_UTIL_CRC32_H
#define SKYBLIP_CORE_UTIL_CRC32_H

#include <cstddef>
#include <cstdint>

namespace skyblip {

uint32_t crc32(const uint8_t* data, size_t bytes, uint32_t crc = 0xFFFFFFFFu);

}  // namespace skyblip

#endif
