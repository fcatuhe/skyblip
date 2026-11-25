#include "protocols/adsl/uplink.h"

#include <cstring>

#include "protocols/adsl/packet.h"
#include "protocols/common/crc.h"
#include "protocols/common/xxtea.h"

namespace skyblip::adsl {

using wire::Status;

namespace {

constexpr uint8_t kPayloadTypeTrafficUplink = 0x04;
constexpr int64_t kCycleMs = int64_t{kTimestampCycleS} * 1000;
constexpr int kQuartersPerCycle = static_cast<int>(kCycleMs / kTimestampQuarterMs);
constexpr uint8_t kReservedBits = 0xC0;

void scramble(uint8_t* data, int bytes, bool forward) {
    uint32_t words[(kUplinkHeaderBytes + uplink_payload_bytes(kUplinkMaxTargets)) / 4];
    const uint32_t count = static_cast<uint32_t>(bytes / 4);
    std::memcpy(words, data, static_cast<size_t>(bytes));
    if (forward)
        wire::xxtea_encrypt(words, count, wire::kXxteaZeroKey, 6);
    else
        wire::xxtea_decrypt(words, count, wire::kXxteaZeroKey, 6);
    std::memcpy(data, words, static_cast<size_t>(bytes));
}

// INFO: fc 13nov26 E.3.2 pads the block with leading zeros to 223 bytes and sends no pad
void codeword_of(const uint8_t* covered, int covered_bytes, const uint8_t* parity,
                 uint8_t* codeword) {
    const int pad = ReedSolomon255::kDataBytes - covered_bytes;
    std::memset(codeword, 0, static_cast<size_t>(pad));
    std::memcpy(codeword + pad, covered, static_cast<size_t>(covered_bytes));
    std::memcpy(codeword + ReedSolomon255::kDataBytes, parity, ReedSolomon255::kParityBytes);
}

}  // namespace

bool uplink_record_current(const Packet& traffic, uint32_t utc_s, int32_t into_ms) {
    if (traffic.timestamp >= kQuartersPerCycle) return false;
    const int64_t now_ms = int64_t{utc_s % kTimestampCycleS} * 1000 + into_ms;
    const int64_t named_ms = int64_t{traffic.timestamp} * kTimestampQuarterMs;
    const int64_t age_ms = ((now_ms - named_ms) % kCycleMs + kCycleMs) % kCycleMs;
    return age_ms <= kUplinkRecordMaxAgeMs;
}

Status Uplink::encode(uint32_t sender_address, uint8_t sender_table, const Packet* traffic,
                      int count, uint8_t* packet) const {
    if (count < 0 || count > kUplinkMaxTargets) return Status::Full;
    const int payload_bytes = uplink_payload_bytes(count);
    const int data_bytes = kUplinkHeaderBytes + payload_bytes;
    const int covered_bytes = 1 + data_bytes;

    uint8_t covered[ReedSolomon255::kDataBytes] = {0};
    covered[0] = static_cast<uint8_t>(Packet::kProtocolVersionIssue2 | Packet::kErrorControlFec);
    Packet header{};
    header.reset(kPayloadTypeTrafficUplink);
    header.set_address(sender_address);
    header.set_address_mapping_table(sender_table);
    std::memcpy(covered + 1, header.payload, kUplinkHeaderBytes);
    for (int i = 0; i < count; i++) {
        uint8_t* record =
            covered + 1 + kUplinkHeaderBytes + static_cast<size_t>(i) * kUplinkRecordBytes;
        std::memcpy(record, traffic[i].payload + 1, 4);
        record[3] = static_cast<uint8_t>(record[3] & ~kReservedBits);
        std::memcpy(record + 4, traffic[i].payload + kUplinkHeaderBytes,
                    Packet::kPayloadBytes - kUplinkHeaderBytes);
    }
    scramble(covered + 1, data_bytes, true);

    uint8_t codeword[ReedSolomon255::kCodewordBytes];
    const uint8_t no_parity[ReedSolomon255::kParityBytes] = {0};
    codeword_of(covered, covered_bytes, no_parity, codeword);
    reed_solomon_.encode(codeword, codeword + ReedSolomon255::kDataBytes);

    packet[0] = static_cast<uint8_t>(uplink_length(count, ErrorControl::Fec));
    std::memcpy(packet + 1, covered, static_cast<size_t>(covered_bytes));
    std::memcpy(packet + 1 + covered_bytes, codeword + ReedSolomon255::kDataBytes,
                ReedSolomon255::kParityBytes);
    return Status::Ok;
}

Status Uplink::decode(const uint8_t* packet, size_t bytes, Packet* traffic, int capacity,
                      UplinkStatistics& statistics) const {
    statistics = UplinkStatistics{};
    if (bytes < 1) return Status::Invalid;
    const int length = packet[0];
    const UplinkLayout layout = uplink_layout(length);
    if (layout.records < 0 || static_cast<size_t>(length) + 1 > bytes) return Status::Invalid;
    const int count = layout.records;
    const int covered_bytes = length - uplink_parity_bytes(layout.mode);

    uint8_t covered[ReedSolomon255::kDataBytes];
    const int corrected = correct(packet + 1, covered_bytes, layout.mode, covered);
    if (corrected < 0) return Status::Crc;
    statistics.corrected = corrected;

    const uint8_t header = covered[0];
    const bool fec_named = (header & Packet::kErrorControlFec) != 0;
    if (fec_named != (layout.mode == ErrorControl::Fec)) return Status::Invalid;
    if ((header & Packet::kProtocolVersionMask) > Packet::kMaxProtocolVersion)
        return Status::Unsupported;
    const uint8_t key_index =
        static_cast<uint8_t>((header >> Packet::kKeyIndexShift) & Packet::kKeyIndexMask);
    if (key_index != Packet::kKeyScrambled && key_index != Packet::kKeyClear)
        return Status::Unsupported;
    const int data_bytes = covered_bytes - 1;
    if (key_index == Packet::kKeyScrambled) scramble(covered + 1, data_bytes, false);
    if (covered[1] != kPayloadTypeTrafficUplink) return Status::Unsupported;

    int kept = 0;
    for (int i = 0; i < count && kept < capacity; i++) {
        const uint8_t* record =
            covered + 1 + kUplinkHeaderBytes + static_cast<size_t>(i) * kUplinkRecordBytes;
        Packet target{};
        target.reset(Packet::kPayloadTypePosition);
        std::memcpy(target.payload + 1, record, 4);
        target.payload[4] = static_cast<uint8_t>(target.payload[4] & ~kReservedBits);
        std::memcpy(target.payload + kUplinkHeaderBytes, record + 4,
                    Packet::kPayloadBytes - kUplinkHeaderBytes);
        target.set_protocol_version(lowest_version(target));
        if (target.address() == 0 || !target.has_position()) {
            statistics.rejected++;
            continue;
        }
        traffic[kept++] = target;
    }
    statistics.targets = kept;
    return Status::Ok;
}

int Uplink::correct(const uint8_t* sent, int covered_bytes, ErrorControl mode,
                    uint8_t* covered) const {
    if (mode == ErrorControl::Crc) {
        const size_t checked = static_cast<size_t>(covered_bytes) + kUplinkCrcBytes;
        if (wire::crc24_check(sent, checked) != 0) return -1;
        std::memcpy(covered, sent, static_cast<size_t>(covered_bytes));
        return 0;
    }
    const int pad = ReedSolomon255::kDataBytes - covered_bytes;
    uint8_t codeword[ReedSolomon255::kCodewordBytes];
    codeword_of(sent, covered_bytes, sent + covered_bytes, codeword);
    const int corrected = reed_solomon_.decode(codeword);
    if (corrected < 0) return -1;
    for (int i = 0; i < pad; i++)
        if (codeword[i] != 0) return -1;
    std::memcpy(covered, codeword + pad, static_cast<size_t>(covered_bytes));
    return corrected;
}

size_t encode_o_band(const uint8_t* packet, uint8_t* out) {
    const size_t bytes = 1u + packet[0];
    for (size_t i = 0; i < sizeof(kUplinkSync); i++) out[i] = kUplinkSync[i];
    for (size_t i = 0; i < bytes; i++) out[sizeof(kUplinkSync) + i] = packet[i];
    return sizeof(kUplinkSync) + bytes;
}

}
