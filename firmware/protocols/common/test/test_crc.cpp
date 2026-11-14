// The two CRCs the air frames carry: the 24-bit one of ADS-L and Mode S, and CRC-16 CCITT of
// ALP-TAS.
#include <cstdint>

#include "doctest/doctest.h"
#include "protocols/common/crc.h"

using namespace skyblip::wire;

TEST_CASE("crc: the ADS-L CRC detects a good vs corrupted packet") {
    uint8_t data[24];
    for (int i = 0; i < 21; i++) data[i] = static_cast<uint8_t>(i * 7 + 1);
    uint32_t crc = crc24_compute(data, 21);
    data[21] = static_cast<uint8_t>(crc >> 16);
    data[22] = static_cast<uint8_t>(crc >> 8);
    data[23] = static_cast<uint8_t>(crc);
    CHECK(crc24_check(data, 24) == 0);
    data[5] ^= 0x10;
    CHECK(crc24_check(data, 24) != 0);
}

// 0x31C3 is the published check value of CRC-16/XMODEM, CCITT with init 0, over "123456789".
TEST_CASE("crc: crc16-ccitt gives the published check value") {
    const uint8_t message[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK(crc16_ccitt(message, 9) == 0x31C3);
}
