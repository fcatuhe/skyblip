// The XXTEA scrambler ADS-L and ALP-TAS share: a scrambler that is not its own inverse corrupts
// every frame, and the avalanche case keeps a long run of identical bits off the air.
#include <cstdint>
#include <cstring>

#include "doctest/doctest.h"
#include "protocols/common/xxtea.h"

using namespace skyblip::wire;

TEST_CASE("scramble: descramble is the exact inverse (property)") {
    for (uint32_t seed = 1; seed < 5000; seed += 137) {
        uint32_t words[5];
        uint32_t state = seed;
        for (int i = 0; i < 5; i++) {
            state = state * 1664525u + 1013904223u;
            words[i] = state;
        }
        uint32_t original[5];
        std::memcpy(original, words, sizeof(words));
        xxtea_encrypt(words, 5, kXxteaZeroKey, 6);
        CHECK(std::memcmp(words, original, sizeof(words)) != 0);  // actually changed
        xxtea_decrypt(words, 5, kXxteaZeroKey, 6);
        CHECK(std::memcmp(words, original, sizeof(words)) == 0);  // and reversible
    }
}

TEST_CASE("scramble: avalanche, one input bit flips ~half the output bits") {
    uint32_t words[5] = {0x11111111, 0x22222222, 0x33333333, 0x44444444, 0x55555555};
    uint32_t scrambled[5];
    std::memcpy(scrambled, words, sizeof(words));
    xxtea_encrypt(scrambled, 5, kXxteaZeroKey, 6);
    words[2] ^= 1;  // flip a single bit
    uint32_t flipped[5];
    std::memcpy(flipped, words, sizeof(words));
    xxtea_encrypt(flipped, 5, kXxteaZeroKey, 6);
    int diff = 0;
    for (int i = 0; i < 5; i++) diff += __builtin_popcountl(scrambled[i] ^ flipped[i]);
    // 160 output bits. A good diffusion is roughly half.
    CHECK(diff > 50);
    CHECK(diff < 110);
}
