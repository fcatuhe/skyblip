#include "protocols/adsl/uplink.h"

#include <cstring>

#include "protocols/adsl/packet.h"
#include "protocols/common/xxtea.h"

namespace skyblip::adsl {

using wire::Status;

namespace {

constexpr uint8_t kPayloadTypeTrafficUplink = 0x04;
constexpr uint8_t kErrorControlFec = 0x80;
constexpr int kQuartersPerCycle = static_cast<int>(kTimestampCycleS * 1000 / kTimestampQuarterMs);
constexpr int kMaxAgeQuarters = static_cast<int>(kUplinkRecordMaxAgeMs / kTimestampQuarterMs);
constexpr uint8_t kReservedBits = 0xC0;

int count_of(int length) {
    for (int count = 0; count <= kUplinkMaxTargets; count++)
        if (uplink_length(count) == length) return count;
    return -1;
}

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
    const int now = timestamp_code(utc_s, into_ms);
    const int age = (now - traffic.timestamp + kQuartersPerCycle) % kQuartersPerCycle;
    return age <= kMaxAgeQuarters;
}

Status Uplink::encode(uint32_t sender_address, uint8_t sender_table, const Packet* traffic,
                      int count, uint8_t* packet) const {
    if (count < 0 || count > kUplinkMaxTargets) return Status::Full;
    const int payload_bytes = uplink_payload_bytes(count);
    const int data_bytes = kUplinkHeaderBytes + payload_bytes;
    const int covered_bytes = 1 + data_bytes;

    uint8_t covered[ReedSolomon255::kDataBytes] = {0};
    covered[0] = static_cast<uint8_t>(Packet::kProtocolVersionIssue2 | kErrorControlFec);
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

    packet[0] = static_cast<uint8_t>(uplink_length(count));
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
    const int count = count_of(length);
    if (count < 0 || static_cast<size_t>(length) + 1 > bytes) return Status::Invalid;
    const int covered_bytes = length - ReedSolomon255::kParityBytes;
    const int pad = ReedSolomon255::kDataBytes - covered_bytes;

    uint8_t codeword[ReedSolomon255::kCodewordBytes];
    codeword_of(packet + 1, covered_bytes, packet + 1 + covered_bytes, codeword);
    const int corrected = reed_solomon_.decode(codeword);
    if (corrected < 0) return Status::Crc;
    for (int i = 0; i < pad; i++)
        if (codeword[i] != 0) return Status::Crc;
    statistics.corrected = corrected;

    uint8_t* covered = codeword + pad;
    if ((covered[0] & kErrorControlFec) == 0) return Status::Invalid;
    const int data_bytes = covered_bytes - 1;
    scramble(covered + 1, data_bytes, false);
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
        if (target.address() == 0 || !target.has_position()) {
            statistics.rejected++;
            continue;
        }
        target.set_protocol_version(lowest_version(target));
        traffic[kept++] = target;
    }
    statistics.targets = kept;
    return Status::Ok;
}

size_t encode_o_band(const uint8_t* packet, uint8_t* out) {
    const size_t bytes = 1u + packet[0];
    for (size_t i = 0; i < sizeof(kUplinkSync); i++) out[i] = kUplinkSync[i];
    for (size_t i = 0; i < bytes; i++) out[sizeof(kUplinkSync) + i] = packet[i];
    return sizeof(kUplinkSync) + bytes;
}

}
