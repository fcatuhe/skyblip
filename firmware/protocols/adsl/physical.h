#ifndef SKYBLIP_PROTOCOLS_ADSL_PHYSICAL_H
#define SKYBLIP_PROTOCOLS_ADSL_PHYSICAL_H

#include <cstdint>

#include "protocols/adsl/packet.h"

namespace skyblip::adsl {

constexpr uint32_t kMBand0Hz = 868200000;
constexpr uint32_t kMBand1Hz = 868400000;
constexpr uint32_t kOBandHz = 869525000;

constexpr uint32_t channel_hz(int channel) { return channel == 1 ? kMBand1Hz : kMBand0Hz; }

// INFO: fc 18sep26 §C.2's table, clocked at the chip rate because §C.2.1's Manchester halves it
constexpr uint32_t kMBandChipRateBps = 100000;
constexpr uint32_t kMBandDeviationHz = 50000;
constexpr uint32_t kMBandChannelBandwidthHz = 200000;
constexpr uint16_t kMBandGaussianBtE2 = 50;
constexpr int8_t kMBandErpCeilingDbm = 14;

// INFO: fc 13nov26 §C.2.4's 0x72 0x4B behind the preamble's last byte, then §D.1.1's length, 24
constexpr uint32_t kSyncWord = 0xF5724B18u;

constexpr uint8_t kFrameBytes = Packet::kDataBytes;

static_assert((kSyncWord & 0xFFu) == kFrameBytes, "the sync word's last byte is the length");

}

#endif
