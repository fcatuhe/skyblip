#include "protocols/common/xxtea.h"

namespace skyblip::wire {

namespace {

constexpr uint32_t kDelta = 0x9E3779B9u;

uint32_t mix(uint32_t after, uint32_t before, uint32_t sum, uint32_t key_word) {
    return (((before >> 5) ^ (after << 2)) + ((after >> 3) ^ (before << 4))) ^
           ((sum ^ after) + (key_word ^ before));
}

uint32_t key_word(const XxteaKey& key, uint32_t index, uint32_t sum) {
    return key[(index & 3) ^ ((sum >> 2) & 3)];
}

}

void xxtea_encrypt(uint32_t* words, uint32_t count, const XxteaKey& key, uint32_t rounds) {
    uint32_t sum = 0;
    uint32_t before = words[count - 1];
    while (rounds-- > 0) {
        sum += kDelta;
        for (uint32_t index = 0; index < count; index++) {
            const uint32_t after = words[(index + 1) % count];
            words[index] += mix(after, before, sum, key_word(key, index, sum));
            before = words[index];
        }
    }
}

void xxtea_decrypt(uint32_t* words, uint32_t count, const XxteaKey& key, uint32_t rounds) {
    uint32_t sum = rounds * kDelta;
    uint32_t after = words[0];
    while (rounds-- > 0) {
        for (uint32_t index = count; index-- > 0;) {
            const uint32_t before = words[(index + count - 1) % count];
            words[index] -= mix(after, before, sum, key_word(key, index, sum));
            after = words[index];
        }
        sum -= kDelta;
    }
}

}
