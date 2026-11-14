#ifndef SKYBLIP_CORE_FEC_CRC_H
#define SKYBLIP_CORE_FEC_CRC_H

#include <cstddef>
#include <cstdint>

namespace skyblip::fec {

uint32_t crc32(const uint8_t* data, size_t bytes, uint32_t crc = 0xFFFFFFFFu);

}

#endif
