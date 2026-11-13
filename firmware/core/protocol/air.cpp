#include "core/protocol/air.h"

#include "core/model/band.h"
#include "protocols/common/manchester.h"

namespace skyblip::protocol {

namespace {

constexpr uint32_t kTailMask = sync_tail(0xFFFFFFFFu);
// A dead chip pair decodes to a wrong bit, and the tail is only 23 of them.
constexpr int kSyncTailErrorsCeiling = 1;

uint32_t tail_of(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 16) | (static_cast<uint32_t>(data[1]) << 8) | data[2];
}

int tail_errors(const uint8_t* data, uint32_t sync_word) {
    return __builtin_popcount((tail_of(data) ^ sync_tail(sync_word)) & kTailMask);
}

System system_of(const uint8_t* data) {
    if (tail_errors(data, adsl::kSyncWord) <= kSyncTailErrorsCeiling) return System::AdslDirect;
    if (tail_errors(data, alptas::kSyncWord) <= kSyncTailErrorsCeiling) return System::Alptas;
    return System::Unknown;
}

void shift_left(uint8_t* data, size_t bytes, uint8_t bits) {
    const size_t byte_offset = bits >> 3;
    const uint8_t rest = bits & 7;
    for (size_t i = 0; i < bytes; i++) {
        const size_t source = i + byte_offset;
        const uint32_t high = source < bytes ? data[source] : 0u;
        const uint32_t low = source + 1 < bytes ? data[source + 1] : 0u;
        data[i] = static_cast<uint8_t>((high << rest) | (rest != 0 ? low >> (8u - rest) : 0u));
    }
}

}  // namespace

uint8_t frame_bytes(System system) {
    switch (system) {
        case System::AdslDirect: return adsl::kFrameBytes;
        case System::Alptas: return alptas::kFrameBytes;
        case System::AdslUplink: return adsl::kUplinkFrameBytes;
        case System::Unknown: return 0;
    }
    return 0;
}

size_t m_band_payload(uint32_t sync_word, const uint8_t* frame, uint8_t bytes, uint8_t* chips) {
    uint8_t tail[kSyncTailBytes] = {0};
    for (int i = 0; i < kSyncTailBytes; i++)
        tail[i] = static_cast<uint8_t>(sync_word >> (8 * (kSyncTailBytes - 1 - i)));
    wire::manchester_encode(tail, sizeof(tail), chips);
    wire::manchester_encode(frame, bytes, chips + kSyncTailChipBytes);
    return kSyncTailChipBytes + 2u * bytes;
}

bool framed_noise(const Frame& frame) {
    if (frame.system != System::Unknown) return false;
    uint16_t bad = 0;
    for (uint8_t i = 0; i < kNoiseWindowBytes; i++)
        bad = static_cast<uint16_t>(bad + __builtin_popcount(frame.erasures[i]));
    return bad >= kNoiseBadChips;
}

bool receive_m_band(const uint8_t* chips, size_t chip_bytes, Frame& out) {
    out = Frame{};
    if (chip_bytes < kRxChipBytes) return false;
    wire::manchester_decode(chips, kRxFrameBytes, out.data, out.erasures);
    out.system = system_of(out.data);
    if (out.system == System::Unknown) return false;
    shift_left(out.data, kRxFrameBytes, kSyncTailBits);
    shift_left(out.erasures, kRxFrameBytes, kSyncTailBits);
    out.bytes = frame_bytes(out.system);
    // A dwell reads for the longer of the two frames, so the shorter one is
    // followed by chips no transmitter sent. Counting those as damage would call
    // every ADS-L frame broken.
    for (uint8_t i = 0; i < out.bytes; i++)
        out.bad_chips = static_cast<uint16_t>(out.bad_chips + __builtin_popcount(out.erasures[i]));
    return true;
}

System receive_burst(model::Band band, const uint8_t* data, size_t bytes, Frame& out) {
    if (band == model::Band::O) {
        out = Frame{};
        return bytes == adsl::kUplinkFrameBytes ? System::AdslUplink : System::Unknown;
    }
    return receive_m_band(data, bytes, out) ? out.system : System::Unknown;
}

}  // namespace skyblip::protocol
