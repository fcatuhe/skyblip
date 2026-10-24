#include "core/fec/scramble.h"

namespace skyblip::fec {

namespace {
inline uint32_t mix_key0(uint32_t next, uint32_t previous, uint32_t sum) {
    return (((previous >> 5) ^ (next << 2)) + ((next >> 3) ^ (previous << 4))) ^
           ((sum ^ next) + previous);
}
constexpr uint32_t kDelta = 0x9e3779b9u;
}

void xxtea_scramble_key0(uint32_t* data, uint8_t words, uint8_t loops) {
    uint32_t sum = 0;
    uint32_t previous = data[words - 1];
    uint32_t next;
    for (; loops; loops--) {
        sum += kDelta;
        for (uint8_t index = 0; index < static_cast<uint8_t>(words - 1); index++) {
            next = data[index + 1];
            previous = data[index] += mix_key0(next, previous, sum);
        }
        next = data[0];
        previous = data[words - 1] += mix_key0(next, previous, sum);
    }
}

void xxtea_descramble_key0(uint32_t* data, uint8_t words, uint8_t loops) {
    uint32_t sum = loops * kDelta;
    uint32_t next = data[0];
    uint32_t previous;
    for (; loops; loops--) {
        for (uint8_t index = static_cast<uint8_t>(words - 1); index; index--) {
            previous = data[index - 1];
            next = data[index] -= mix_key0(next, previous, sum);
        }
        previous = data[words - 1];
        next = data[0] -= mix_key0(next, previous, sum);
        sum -= kDelta;
    }
}

}
