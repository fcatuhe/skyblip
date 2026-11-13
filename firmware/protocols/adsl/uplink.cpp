#include "protocols/adsl/uplink.h"

#include <cstring>

#include "protocols/common/little_endian.h"
#include "protocols/common/xxtea.h"

namespace skyblip::adsl {

using wire::get_int16;
using wire::get_int32;
using wire::get_uint24;
using wire::put_int16;
using wire::put_int32;
using wire::put_uint24;
using wire::Status;

namespace {
void pack_record(uint8_t* record, const UplinkTarget& target) {
    put_uint24(record, target.address);
    record[3] = static_cast<uint8_t>((target.address_mapping_table & 0x3F) |
                                     ((target.flight_state & 0x03) << 6));
    record[4] = target.aircraft_category;
    record[5] = static_cast<uint8_t>(target.speed_q > 255 ? 255 : target.speed_q);
    put_int32(record + 6, target.latitude_1e7);
    put_int32(record + 10, target.longitude_1e7);
    put_int16(record + 14, static_cast<int16_t>(target.altitude_m));
}

void unpack_record(const uint8_t* record, UplinkTarget& target) {
    target = UplinkTarget{};
    target.address = get_uint24(record);
    target.address_mapping_table = record[3] & 0x3F;
    target.flight_state = (record[3] >> 6) & 0x03;
    target.aircraft_category = record[4];
    target.speed_q = record[5];
    target.latitude_1e7 = get_int32(record + 6);
    target.longitude_1e7 = get_int32(record + 10);
    target.altitude_m = get_int16(record + 14);
}

// The gate between a codeword that satisfied parity and a target on a pilot's
// screen. RS(255,223) can decode to a valid codeword that is not the one that
// was sent (test/test_uplink.cpp measures how often), and a ground station
// is one transmitter reaching every aircraft in range: one miscorrected frame
// would otherwise put a whole screenful of phantoms in front of everybody at
// once. An address of zero is no aircraft, and a position off the globe is no
// position.
bool plausible(const UplinkTarget& target) {
    if (target.address == 0) return false;
    if (target.latitude_1e7 < -900000000 || target.latitude_1e7 > 900000000) return false;
    if (target.longitude_1e7 < -1800000000 || target.longitude_1e7 > 1800000000) return false;
    return true;
}
}

Status Uplink::encode(const UplinkTarget* targets, int count, uint8_t key_index,
                      uint8_t out_frame[kFrameBytes]) const {
    if (count < 0 || count > kMaxTargets) return Status::Full;
    uint8_t data[ReedSolomon255::kDataBytes];
    std::memset(data, 0, sizeof(data));
    data[0] = kVersion;
    data[1] = static_cast<uint8_t>(count);
    data[2] = key_index;
    for (int i = 0; i < count; i++) {
        pack_record(data + kHeaderBytes + static_cast<ptrdiff_t>(i) * kRecordBytes, targets[i]);
    }
    uint32_t words[ReedSolomon255::kDataBytes / 4];
    std::memcpy(words, data, sizeof(words));
    wire::xxtea_encrypt(words, sizeof(words) / 4, wire::kXxteaZeroKey, 6);
    std::memcpy(data, words, sizeof(words));

    std::memcpy(out_frame, data, ReedSolomon255::kDataBytes);
    reed_solomon_.encode(data, out_frame + ReedSolomon255::kDataBytes);
    return Status::Ok;
}

Status Uplink::decode(const uint8_t frame[kFrameBytes], UplinkTarget* targets, int capacity,
                      DecodeStatistics& statistics) const {
    uint8_t codeword[ReedSolomon255::kCodewordBytes];
    std::memcpy(codeword, frame, sizeof(codeword));
    int corrected = reed_solomon_.decode(codeword);
    if (corrected < 0) return Status::Crc;
    statistics.corrected = corrected;

    uint8_t data[ReedSolomon255::kDataBytes];
    std::memcpy(data, codeword, ReedSolomon255::kDataBytes);
    uint32_t words[ReedSolomon255::kDataBytes / 4];
    std::memcpy(words, data, sizeof(words));
    wire::xxtea_decrypt(words, sizeof(words) / 4, wire::kXxteaZeroKey, 6);
    std::memcpy(data, words, sizeof(words));

    if (data[0] != kVersion) return Status::Unsupported;
    int carried = data[1];
    if (carried > kMaxTargets) return Status::Invalid;
    int kept = 0;
    for (int i = 0; i < carried && kept < capacity; i++) {
        UplinkTarget target{};
        unpack_record(data + kHeaderBytes + static_cast<ptrdiff_t>(i) * kRecordBytes, target);
        if (!plausible(target)) {
            statistics.rejected++;
            continue;
        }
        targets[kept++] = target;
    }
    statistics.targets = kept;
    return Status::Ok;
}

size_t encode_o_band(const uint8_t* frame, uint8_t* out) {
    for (size_t i = 0; i < sizeof(kUplinkSync); i++) out[i] = kUplinkSync[i];
    for (size_t i = 0; i < kUplinkFrameBytes; i++) out[sizeof(kUplinkSync) + i] = frame[i];
    return sizeof(kUplinkSync) + kUplinkFrameBytes;
}

}
