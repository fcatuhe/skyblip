// core/protocol/air.h: one M-band dwell, two systems: ADS-L 4 SRD-860 issue 2
// §C.2 and the FLARM-generation wire this project calls ALP-TAS. Both Manchester
// their frames behind a 32-bit sync word, and the two sync words agree on their
// first 18 chips, so a receiver armed with 16 of those chips detects either one
// and software names the system from the chips that follow.
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

// What the detector consumed is gone from the report, so the frame arrives
// behind the rest of whichever sync word it was.
constexpr uint8_t kSyncTailBits = static_cast<uint8_t>(32u - kSharedSyncEndChip / 2u);
constexpr uint8_t kSyncTailBytes = 3;

static_assert(kSyncTailBits == 8u * kSyncTailBytes,
              "a radio inserts whole bytes, so the window has to end on one");

constexpr uint32_t sync_tail(uint32_t sync_word) {
    return (sync_word & ((1u << kSyncTailBits) - 1u)) << (8u * kSyncTailBytes - kSyncTailBits);
}

// Read the sync tail plus the longer of the two frames, and read it as chips.
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
    uint16_t bad_chips{0};
    uint8_t data[kRxFrameBytes]{};
    uint8_t erasures[kRxFrameBytes]{};
};

uint8_t frame_bytes(System system);

// INFO: fc 15sep26 SX1262 DS 6.2.2: the part inserts the window, the buffer carries the rest
size_t m_band_payload(uint32_t sync_word, const uint8_t* frame, uint8_t bytes, uint8_t* chips);

// The chips a receiver reports once the shared window matched. Manchester-decode
// them, name the system by the sync tail, and shift the frame to byte zero.
bool receive_m_band(const uint8_t* chips, size_t chip_bytes, Frame& out);

// INFO: fc 17sep26 noise decodes half its chip pairs to a pair no transmitter can send
constexpr uint8_t kNoiseWindowBytes = 12;
constexpr uint16_t kNoiseBadChips = kNoiseWindowBytes * 8u / 4u;

static_assert(kNoiseWindowBytes <= kSyncTailBytes + adsl::kFrameBytes,
              "the window has to end inside the shortest burst this band carries");

bool framed_noise(const Frame& frame);

// One burst off the bus, named by the dwell that heard it.
//
// The M band carries two systems past one shared sync window, so there the name
// has to be Manchester-decoded out of the sync tail. The O band carries the
// ground station's uplink and nothing else: the dwell was armed for §C.4.3's
// sync word, and the §D.1.1 length byte behind it names one of G.3's eleven
// packet lengths or the burst is not an uplink packet. out is filled for the M
// band only - an uplink packet is nine times longer than a Frame and stays where
// the executor already put it.
//
// Naming a burst is protocol policy, so it lives here rather than in the
// service that pulls the event: the service moves messages.
System receive_burst(model::Band band, const uint8_t* data, size_t bytes, Frame& out);

}  // namespace skyblip::protocol

#endif
