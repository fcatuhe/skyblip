#ifndef SKYBLIP_PROTOCOLS_ADSL_UPLINK_H
#define SKYBLIP_PROTOCOLS_ADSL_UPLINK_H

#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "protocols/adsl/packet.h"
#include "protocols/adsl/reed_solomon.h"
#include "protocols/common/status.h"

namespace skyblip::adsl {

constexpr uint8_t kUplinkSync[2] = {0x2D, 0xD4};
constexpr uint8_t kUplinkSyncBits = 16;

// INFO: fc 20nov26 §C.4's rate, shaping and bandwidth differ from §C.2's, so a dwell sets all three
constexpr uint32_t kUplinkChipRateBps = 200000;
constexpr uint32_t kUplinkDeviationHz = 50000;
constexpr uint32_t kUplinkChannelBandwidthHz = 250000;
constexpr uint16_t kUplinkGaussianBtE2 = 50;

// INFO: fc 20nov26 §C.4.2: the preamble is "10" twelve times, ahead of the sync word
constexpr uint16_t kUplinkPreambleChips = 24;

// INFO: fc 13nov26 G.3: a 30-bit address, 2 reserved bits and the 120-bit Traffic payload, verbatim
constexpr int kUplinkRecordBytes = 19;
constexpr int kUplinkMaxTargets = 10;
constexpr int kUplinkHeaderBytes = 5;
constexpr int kUplinkCrcBytes = 3;

enum class ErrorControl : uint8_t { Crc, Fec };

constexpr int uplink_parity_bytes(ErrorControl mode) {
    return mode == ErrorControl::Fec ? ReedSolomon255::kParityBytes : kUplinkCrcBytes;
}

// INFO: fc 13nov26 F.2.5: the payload is padded so that it is 3 bytes past a whole number of words
constexpr int uplink_payload_bytes(int count) {
    return kUplinkRecordBytes * count + (3 - kUplinkRecordBytes * count % 4 + 4) % 4;
}
// INFO: fc 13nov26 D.1.1's Packet Length: network header, ADS-L data and parity, not itself
constexpr int uplink_length(int count, ErrorControl mode) {
    return 1 + kUplinkHeaderBytes + uplink_payload_bytes(count) + uplink_parity_bytes(mode);
}

struct UplinkLayout {
    int records{-1};
    ErrorControl mode{ErrorControl::Crc};
};

// INFO: fc 22nov26 G.3 counts the records from the length, and no CRC length is also a FEC one
constexpr UplinkLayout uplink_layout(int length) {
    for (const ErrorControl mode : {ErrorControl::Crc, ErrorControl::Fec})
        for (int count = 0; count <= kUplinkMaxTargets; count++)
            if (uplink_length(count, mode) == length) return {count, mode};
    return {};
}

constexpr int kUplinkMaxLength = uplink_length(kUplinkMaxTargets, ErrorControl::Fec);
static_assert(kUplinkMaxLength <= 255, "the §D.1.1 length field is one byte wide");
static_assert(1 + kUplinkHeaderBytes + uplink_payload_bytes(kUplinkMaxTargets) <=
                  ReedSolomon255::kDataBytes,
              "E.3.2 covers at most 223 bytes");
constexpr int kUplinkMaxBurstBytes = static_cast<int>(sizeof(kUplinkSync)) + 1 + kUplinkMaxLength;

// INFO: fc 20nov26 the dwell map in core/timing/slot.h is cut against the longest uplink burst
constexpr uint32_t kUplinkBurstBits =
    kUplinkPreambleChips + kUplinkSyncBits + 8u * (1u + kUplinkMaxLength);
constexpr uint32_t kUplinkBurstUs = kUplinkBurstBits * 1000000u / kUplinkChipRateBps;

// INFO: fc 13nov26 G.3: a relayed Traffic payload no older than 10 s and not from the future
constexpr uint32_t kUplinkRecordMaxAgeMs = 10000;
bool uplink_record_current(const Packet& traffic, uint32_t utc_s, int32_t into_ms);

struct UplinkStatistics {
    int targets{0};
    int corrected{0};
    int rejected{0};
};

class Uplink {
   public:
    // INFO: fc 13nov26 packet is the Packet Length byte and what follows it, as D.1.1 sends them
    wire::Status encode(uint32_t sender_address, uint8_t sender_table, const Packet* traffic,
                        int count, uint8_t* packet) const;
    wire::Status decode(const uint8_t* packet, size_t bytes, Packet* traffic, int capacity,
                        UplinkStatistics& statistics) const;

   private:
    int correct(const uint8_t* sent, int covered_bytes, ErrorControl mode, uint8_t* covered) const;

    ReedSolomon255 reed_solomon_;
};

size_t encode_o_band(const uint8_t* packet, uint8_t* out);

}

#endif
