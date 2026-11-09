#include "core/fec/crc.h"

namespace skyblip::fec {

uint32_t crc32(const uint8_t* data, size_t bytes, uint32_t crc) {
    for (size_t i = 0; i < bytes; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

}
