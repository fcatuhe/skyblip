// Manchester coding checked in both directions: a decoder that hides a flipped chip instead of
// flagging it corrupts traffic the CRC then has to catch by luck.
#include <cstdint>
#include <cstring>

#include "doctest/doctest.h"
#include "protocols/common/manchester.h"

using namespace skyblip::wire;

TEST_CASE("manchester: encode/decode round-trip is clean") {
    uint8_t data[21];
    for (int i = 0; i < 21; i++) data[i] = static_cast<uint8_t>(i * 11 + 3);
    uint8_t coded[42];
    manchester_encode(data, 21, coded);
    uint8_t out[21], error[21];
    size_t bad = manchester_decode(coded, 21, out, error);
    CHECK(bad == 0);
    CHECK(std::memcmp(data, out, 21) == 0);
    for (int i = 0; i < 21; i++) CHECK(error[i] == 0);
}

TEST_CASE("manchester: a flipped chip is flagged as an error bit") {
    uint8_t data[4] = {0xA5, 0x00, 0xFF, 0x3C};
    uint8_t coded[8];
    manchester_encode(data, 4, coded);
    coded[0] ^= 0x80;  // break one Manchester symbol -> illegal 00/11
    uint8_t out[4], error[4];
    size_t bad = manchester_decode(coded, 4, out, error);
    CHECK(bad >= 1);
    CHECK(error[0] != 0);
}
