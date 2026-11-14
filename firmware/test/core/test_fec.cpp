// The CRC-32 the stores seal records with, checked for stability and for a flipped bit.
#include <cstdint>
#include <cstring>

#include "core/fec/crc.h"
#include "doctest/doctest.h"

using namespace skyblip::fec;

TEST_CASE("crc: crc32 known/stability") {
    const uint8_t message[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    uint32_t first = crc32(message, 9);
    uint32_t second = crc32(message, 9);
    CHECK(first == second);
    uint8_t flipped_message[9];
    std::memcpy(flipped_message, message, 9);
    flipped_message[0] ^= 1;
    CHECK(crc32(flipped_message, 9) != first);
}
