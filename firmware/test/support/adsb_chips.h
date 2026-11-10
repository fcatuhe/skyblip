#ifndef SKYBLIP_TEST_SUPPORT_ADSB_CHIPS_H
#define SKYBLIP_TEST_SUPPORT_ADSB_CHIPS_H

#include <cstdint>

#include "core/fec/crc.h"
#include "core/fec/manchester.h"
#include "core/protocol/adsb.h"

namespace skyblip {

constexpr uint8_t kAdsbParityOffsetBytes = 11;

inline void adsb_seal(uint8_t* frame) {
    const uint32_t parity = fec::adsl_crc_compute(frame, kAdsbParityOffsetBytes);
    frame[kAdsbParityOffsetBytes] = static_cast<uint8_t>(parity >> 16);
    frame[kAdsbParityOffsetBytes + 1] = static_cast<uint8_t>(parity >> 8);
    frame[kAdsbParityOffsetBytes + 2] = static_cast<uint8_t>(parity);
}

// INFO: fc 10nov26 1090 sends a 1 as the chips 10, the inverse of manchester_encode's ADS-L table
inline void adsb_chips(const uint8_t* frame, uint8_t* chips) {
    uint8_t coded[2 * protocol::kAdsbFrameBytes];
    fec::manchester_encode(frame, protocol::kAdsbFrameBytes, coded);
    for (uint8_t i = 0; i < protocol::kAdsbChipBytes; i++)
        chips[i] = static_cast<uint8_t>(~coded[i + 1]);
}

}

#endif
