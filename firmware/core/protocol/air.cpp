#include "core/protocol/air.h"

#include "core/model/band.h"
#include "protocols/common/manchester.h"

namespace skyblip::protocol {

namespace {

constexpr int adsl_preamble_repetitions(uint8_t hardware_preamble) {
    uint32_t prefix =
        (uint32_t{hardware_preamble} << 16) | (uint32_t{kAdslTxSync[0]} << 8) | kAdslTxSync[1];
    int repetitions = 0;
    for (; (prefix & 0b11u) == 0b01u; prefix >>= 2) repetitions++;
    return repetitions;
}

static_assert(kAdslTxPreambleChips == 8 && kAdslTxSync[2] == 0x99 &&
              adsl_preamble_repetitions(0x55) == 7 && adsl_preamble_repetitions(0xAA) == 7);

size_t encode_fifo(uint8_t first_byte, const uint8_t* frame, uint8_t bytes, uint8_t* chips) {
    wire::manchester_encode(&first_byte, 1, chips);
    wire::manchester_encode(frame, bytes, chips + 2);
    return 2 + 2u * bytes;
}

// INFO: fc 20nov26 a dead chip pair decodes to a wrong bit, so the short tail allows one
constexpr int kSyncTailErrorsCeiling = 1;

uint32_t tail_of(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 16) | (static_cast<uint32_t>(data[1]) << 8) | data[2];
}

System system_of(const uint8_t* data) {
    const uint32_t tail = tail_of(data);
    const int adsl_errors = __builtin_popcount((tail ^ sync_tail(adsl::kSyncWord)) >> 8);
    if (adsl_errors <= kSyncTailErrorsCeiling) return System::AdslDirect;
    const int alptas_errors = __builtin_popcount(tail ^ sync_tail(alptas::kSyncWord));
    if (alptas_errors <= kSyncTailErrorsCeiling) return System::Alptas;
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

bool uplink_packet(const uint8_t* data, size_t bytes) {
    if (bytes < 1 || static_cast<size_t>(data[0]) + 1 > bytes) return false;
    return adsl::uplink_layout(data[0]).records >= 0;
}

}  // namespace

uint8_t frame_bytes(System system) {
    switch (system) {
        case System::AdslDirect: return adsl::kFrameBytes;
        case System::Alptas: return alptas::kFrameBytes;
        case System::AdslUplink: return static_cast<uint8_t>(1 + adsl::kUplinkMaxLength);
        case System::Unknown: return 0;
    }
    return 0;
}

size_t m_band_payload(uint32_t sync_word, const uint8_t* frame, uint8_t bytes, uint8_t* chips) {
    const uint8_t tail[] = {static_cast<uint8_t>(sync_word >> 16),
                            static_cast<uint8_t>(sync_word >> 8)};
    wire::manchester_encode(tail, sizeof(tail), chips);
    return 2 * sizeof(tail) +
           encode_fifo(static_cast<uint8_t>(sync_word), frame, bytes, chips + 2 * sizeof(tail));
}

size_t adsl_m_band_fifo(const uint8_t* frame, uint8_t bytes, uint8_t* chips) {
    return encode_fifo(bytes, frame, bytes, chips);
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
    const System system = system_of(out.data);
    if (system == System::Unknown) return false;

    out.bytes = frame_bytes(system);
    uint8_t guaranteed_bytes = out.bytes;
    if (system == System::AdslDirect) {
        out.length_heard = out.data[kSyncTailBytes - 1];
        out.length_erasures = out.erasures[kSyncTailBytes - 1];
        const bool short_allowed = adsl::length_allows(out.length_heard, out.length_erasures,
                                                       adsl::Packet::kStatusDataBytes);
        const bool long_allowed =
            adsl::length_allows(out.length_heard, out.length_erasures, adsl::Packet::kDataBytes);
        if (!short_allowed && !long_allowed) {
            out.bytes = 0;
            return false;
        }
        out.bytes = long_allowed ? adsl::Packet::kDataBytes : adsl::Packet::kStatusDataBytes;
        guaranteed_bytes =
            short_allowed ? adsl::Packet::kStatusDataBytes : adsl::Packet::kDataBytes;
    }
    out.system = system;
    shift_left(out.data, kRxFrameBytes, kSyncTailBits);
    shift_left(out.erasures, kRxFrameBytes, kSyncTailBits);
    // INFO: fc 23nov26 chips beyond the shortest candidate may be noise after the burst ended
    for (uint8_t i = 0; i < guaranteed_bytes; i++)
        out.bad_chips = static_cast<uint16_t>(out.bad_chips + __builtin_popcount(out.erasures[i]));
    return true;
}

System receive_burst(model::Band band, const uint8_t* data, size_t bytes, Frame& out) {
    if (band == model::Band::O) {
        out = Frame{};
        return uplink_packet(data, bytes) ? System::AdslUplink : System::Unknown;
    }
    return receive_m_band(data, bytes, out) ? out.system : System::Unknown;
}

}  // namespace skyblip::protocol
