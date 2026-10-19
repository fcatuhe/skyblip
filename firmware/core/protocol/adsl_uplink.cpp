#include "core/protocol/adsl_uplink.h"

#include <cstring>

#include "core/fec/scramble.h"
#include "core/model/aircraft.h"
#include "core/settings/address.h"
#include "core/util/little_endian.h"

namespace skyblip::protocol {

namespace {
void pack_record(uint8_t* r, const model::AircraftObservation& target) {
    put_u24(r, target.address & settings::kAddressMask);
    r[3] = static_cast<uint8_t>((target.address_mapping_table & 0x3F) |
                                ((target.flight_state & 0x03) << 6));
    r[4] = target.aircraft_cat;
    r[5] = static_cast<uint8_t>(target.speed_q > 255 ? 255 : target.speed_q);
    put_i32(r + 6, target.lat_1e7);
    put_i32(r + 10, target.lon_1e7);
    put_i16(r + 14, static_cast<int16_t>(target.alt_m));
}

void unpack_record(const uint8_t* r, model::AircraftObservation& target) {
    target = model::AircraftObservation{};
    target.address = get_u24(r);
    target.address_mapping_table = r[3] & 0x3F;
    target.flight_state = (r[3] >> 6) & 0x03;
    target.aircraft_cat = r[4];
    target.speed_q = r[5];
    target.speed_valid = r[5] != 0;
    target.lat_1e7 = get_i32(r + 6);
    target.lon_1e7 = get_i32(r + 10);
    target.alt_m = get_i16(r + 14);
    target.position_valid = true;
    target.source = model::Source::AdslUplink;
    target.emergency = 1;
}

// The gate between a codeword that satisfied parity and a target on a pilot's
// screen. RS(255,223) can decode to a valid codeword that is not the one that
// was sent (test/core/test_uplink.cpp measures how often), and a ground station
// is one transmitter reaching every aircraft in range: one miscorrected frame
// would otherwise put a whole screenful of phantoms in front of everybody at
// once. An address of zero is no aircraft, and a position off the globe is no
// position.
bool plausible(const model::AircraftObservation& target) {
    if (target.address == 0) return false;
    if (target.lat_1e7 < -900000000 || target.lat_1e7 > 900000000) return false;
    if (target.lon_1e7 < -1800000000 || target.lon_1e7 > 1800000000) return false;
    return true;
}
}

Status AdslUplink::encode(const model::AircraftObservation* targets, int count, uint8_t key_index,
                          uint8_t out_frame[kFrameBytes]) const {
    if (count < 0 || count > kMaxTargets) return Status::Full;
    uint8_t data[fec::ReedSolomon255::kK];
    std::memset(data, 0, sizeof(data));
    data[0] = kVersion;
    data[1] = static_cast<uint8_t>(count);
    data[2] = key_index;
    for (int i = 0; i < count; i++) {
        pack_record(data + kHeaderBytes + static_cast<ptrdiff_t>(i) * kRecordBytes, targets[i]);
    }
    uint32_t words[fec::ReedSolomon255::kK / 4];
    std::memcpy(words, data, sizeof(words));
    fec::xxtea_scramble_key0(words, sizeof(words) / 4, 6);
    std::memcpy(data, words, sizeof(words));

    std::memcpy(out_frame, data, fec::ReedSolomon255::kK);
    rs_.encode(data, out_frame + fec::ReedSolomon255::kK);
    return Status::Ok;
}

Status AdslUplink::decode(const uint8_t frame[kFrameBytes], model::AircraftObservation* targets,
                          int capacity, DecodeStats& stats) const {
    uint8_t cw[fec::ReedSolomon255::kN];
    std::memcpy(cw, frame, sizeof(cw));
    int corrected = rs_.decode(cw);
    if (corrected < 0) return Status::Crc;
    stats.corrected = corrected;

    uint8_t data[fec::ReedSolomon255::kK];
    std::memcpy(data, cw, fec::ReedSolomon255::kK);
    uint32_t words[fec::ReedSolomon255::kK / 4];
    std::memcpy(words, data, sizeof(words));
    fec::xxtea_descramble_key0(words, sizeof(words) / 4, 6);
    std::memcpy(data, words, sizeof(words));

    if (data[0] != kVersion) return Status::Unsupported;
    int n = data[1];
    if (n > kMaxTargets) return Status::Invalid;
    int out = 0;
    for (int i = 0; i < n && out < capacity; i++) {
        model::AircraftObservation target{};
        unpack_record(data + kHeaderBytes + static_cast<ptrdiff_t>(i) * kRecordBytes, target);
        if (!plausible(target)) {
            stats.rejected++;
            continue;
        }
        targets[out++] = target;
    }
    stats.targets = out;
    return Status::Ok;
}

}
