// INFO: fc 17nov26 ADS-L and ALP-TAS sync words share 18 chips, so 16 of them frame either system
#ifndef SKYBLIP_CORE_PROTOCOL_AIR_H
#define SKYBLIP_CORE_PROTOCOL_AIR_H

#include <cstddef>
#include <cstdint>

#include "core/events/rf.h"
#include "core/model/band.h"
#include "protocols/adsl/physical.h"
#include "protocols/adsl/uplink.h"
#include "protocols/alptas/frame.h"
#include "protocols/common/manchester.h"

namespace skyblip::protocol {

enum class System : uint8_t { Unknown, AdslDirect, Alptas, AdslUplink };

static_assert(1 + adsl::kUplinkMaxLength <= events::kRfEventBytes,
              "an uplink frame would be truncated on its way off the radio");

constexpr uint8_t kSharedSyncBits = 16;
constexpr uint8_t kSharedSyncEndChip = kSharedSyncBits;

constexpr uint16_t shared_sync(uint32_t sync_word) {
    const uint32_t first = wire::manchester_chips(static_cast<uint8_t>(sync_word >> 24));
    const uint32_t second = wire::manchester_chips(static_cast<uint8_t>(sync_word >> 16));
    return static_cast<uint16_t>(((first << 16) | second) >> (32u - kSharedSyncEndChip));
}

static_assert(shared_sync(adsl::kSyncWord) == shared_sync(alptas::kSyncWord),
              "the shared chips are what let one dwell hear both systems");

constexpr uint8_t kSharedSync[2] = {static_cast<uint8_t>(shared_sync(adsl::kSyncWord) >> 8),
                                    static_cast<uint8_t>(shared_sync(adsl::kSyncWord))};

// INFO: fc 20nov26 the report drops the window the detector matched, so it opens on the sync tail
constexpr uint8_t kSyncTailBits = static_cast<uint8_t>(32u - kSharedSyncEndChip / 2u);
constexpr uint8_t kSyncTailBytes = 3;

static_assert(kSyncTailBits == 8u * kSyncTailBytes,
              "a radio inserts whole bytes, so the window has to end on one");

constexpr uint32_t sync_tail(uint32_t sync_word) {
    return (sync_word & ((1u << kSyncTailBits) - 1u)) << (8u * kSyncTailBytes - kSyncTailBits);
}

// INFO: fc 20nov26 a dwell reads the sync tail and the longer frame, ALP-TAS's, as chips
constexpr uint8_t kRxFrameBytes = kSyncTailBytes + alptas::kFrameBytes;
constexpr uint8_t kRxChipBytes = 2 * kRxFrameBytes;
constexpr uint8_t kSyncWindowBytes = wire::kSyncWordBytes - kSyncTailBytes;
constexpr uint8_t kSyncWindowChipBytes = 2 * kSyncWindowBytes;
constexpr uint8_t kSyncTailChipBytes = 2 * kSyncTailBytes;
constexpr uint8_t kSyncChipBytes = kSyncWindowChipBytes + kSyncTailChipBytes;
constexpr uint8_t kTxChipBytes = kSyncChipBytes + 2 * alptas::kFrameBytes;
constexpr uint8_t kTxPayloadChipBytes = kSyncTailChipBytes + 2 * alptas::kFrameBytes;

struct Frame {
    System system{System::Unknown};
    uint8_t bytes{0};
    // INFO: fc 18nov26 D.1.1's length byte as heard, the last of the sync tail, outside the CRC
    uint8_t length_heard{0};
    bool length_doubtful{false};
    uint16_t bad_chips{0};
    uint8_t data[kRxFrameBytes]{};
    uint8_t erasures[kRxFrameBytes]{};
};

uint8_t frame_bytes(System system);

// INFO: fc 15sep26 SX1262 DS 6.2.2: the part inserts the window, the buffer carries the rest
size_t m_band_payload(uint32_t sync_word, const uint8_t* frame, uint8_t bytes, uint8_t* chips);

bool receive_m_band(const uint8_t* chips, size_t chip_bytes, Frame& out);

// INFO: fc 17sep26 noise decodes half its chip pairs to a pair no transmitter can send
constexpr uint8_t kNoiseWindowBytes = 12;
constexpr uint16_t kNoiseBadChips = kNoiseWindowBytes * 8u / 4u;

static_assert(kNoiseWindowBytes <= kSyncTailBytes + adsl::Packet::kStatusDataBytes,
              "the window has to end inside the shortest burst this band carries");

bool framed_noise(const Frame& frame);

// INFO: fc 20nov26 out is filled on the M band only, and an uplink stays where the executor put it
System receive_burst(model::Band band, const uint8_t* data, size_t bytes, Frame& out);

}  // namespace skyblip::protocol

#endif
