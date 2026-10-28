#include "core/protocol/alptas.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "core/fec/crc.h"
#include "core/flight/state.h"
#include "core/model/aircraft.h"
#include "core/protocol/nmea_out.h"
#include "core/settings/address.h"
#include "core/util/bitcount.h"
#include "core/util/intmath.h"
#include "core/util/little_endian.h"

namespace skyblip::protocol {

namespace {

constexpr uint8_t kWords = kAlptasDataBytes / 4;
constexpr uint32_t kDelta = 0x9E3779B9u;
constexpr uint32_t kBlockTeaRounds = 6;
constexpr uint32_t kBlockTeaKey[4] = {0xA5F9B21Cu, 0xAB3F9D12u, 0xC6F34E34u, 0xD72FA378u};
constexpr uint32_t kScrambleKey = 0x956F6C77u;

// INFO: fc 09mar26 field positions are bit offsets from the LSB of byte 0, the
// layout a packed little-endian bit-field struct produces. Spelled out rather
// than declared as bit-fields because the wire order must be identical on the
// host and on Cortex-M4, and packed bit-field allocation is compiler policy, not
// a language guarantee.
struct Field {
    uint16_t first_bit;
    uint8_t width;
};
constexpr Field kAddressField{0, 24};
constexpr Field kMessageTypeField{24, 4};
constexpr Field kAddressTypeField{28, 3};
constexpr Field kNeedsThreeField{56, 4};
constexpr Field kHasThreeField{60, 4};
constexpr Field kTimeBitsField{66, 4};
constexpr Field kAircraftTypeField{70, 4};
constexpr Field kAltitudeField{75, 13};
constexpr Field kLatitudeField{88, 20};
constexpr Field kLongitudeField{108, 20};
constexpr Field kTurnRateField{128, 9};
constexpr Field kSpeedField{137, 10};
constexpr Field kClimbField{147, 9};
constexpr Field kCourseField{156, 10};
constexpr Field kAirborneField{166, 2};
constexpr Field kGnssAccuracyFirstField{168, 6};
constexpr Field kGnssAccuracySecondField{174, 5};
constexpr Field kUnknownField{179, 5};
constexpr Field kLastByteField{184, 8};

uint32_t get_field(const uint8_t* data, Field field) {
    uint32_t value = 0;
    for (uint8_t i = 0; i < field.width; i++) {
        uint16_t bit = static_cast<uint16_t>(field.first_bit + i);
        if (data[bit >> 3] & (1u << (bit & 7))) value |= 1u << i;
    }
    return value;
}

void put_field(uint8_t* data, Field field, uint32_t value) {
    for (uint8_t i = 0; i < field.width; i++) {
        uint16_t bit = static_cast<uint16_t>(field.first_bit + i);
        uint8_t mask = static_cast<uint8_t>(1u << (bit & 7));
        if (value & (1u << i))
            data[bit >> 3] |= mask;
        else
            data[bit >> 3] = static_cast<uint8_t>(data[bit >> 3] & ~mask);
    }
}

uint32_t block_tea_mix(uint32_t next, uint32_t previous, uint32_t sum, uint32_t index,
                       uint32_t key_index) {
    return (((previous >> 5) ^ (next << 2)) + ((next >> 3) ^ (previous << 4))) ^
           ((sum ^ next) + (kBlockTeaKey[(index & 3) ^ key_index] ^ previous));
}

void block_tea(uint32_t* words, bool encode) {
    constexpr uint32_t count = 4;
    uint32_t next, previous, sum, key_index;
    if (encode) {
        sum = 0;
        previous = words[count - 1];
        for (uint32_t round = 0; round < kBlockTeaRounds; round++) {
            sum += kDelta;
            key_index = (sum >> 2) & 3;
            for (uint32_t index = 0; index < count - 1; index++) {
                next = words[index + 1];
                words[index] += block_tea_mix(next, previous, sum, index, key_index);
                previous = words[index];
            }
            next = words[0];
            words[count - 1] += block_tea_mix(next, previous, sum, count - 1, key_index);
            previous = words[count - 1];
        }
    } else {
        sum = kBlockTeaRounds * kDelta;
        next = words[0];
        for (uint32_t round = 0; round < kBlockTeaRounds; round++) {
            key_index = (sum >> 2) & 3;
            for (uint32_t index = count - 1; index > 0; index--) {
                previous = words[index - 1];
                words[index] -= block_tea_mix(next, previous, sum, index, key_index);
                next = words[index];
            }
            previous = words[count - 1];
            words[0] -= block_tea_mix(next, previous, sum, 0, key_index);
            next = words[0];
            sum -= kDelta;
        }
    }
}

// INFO: fc 09mar26 second crypto stage: a byte-wide XXTEA-shaped mixer over the
// plaintext first two words, the UTC second and a fixed key, XORed into the last
// four words. It keys on time, which is why a receiver must know the second the
// frame arrived in.
void scramble(uint32_t* data, uint32_t utc_s) {
    uint8_t keys[16];
    put_uint32(keys + 0, data[0]);
    put_uint32(keys + 4, data[1]);
    put_uint32(keys + 8, utc_s >> 4);
    put_uint32(keys + 12, kScrambleKey);

    uint32_t previous = keys[15];
    uint32_t sum = 0;
    for (int round = 0; round < 2; round++) {
        sum += kDelta;
        uint32_t next = keys[0];
        for (int index = 0; index < 15; index++) {
            uint32_t mixed = next;
            next = keys[index + 1];
            mixed += ((((previous >> 5) ^ (next << 2)) + ((next >> 3) ^ (previous << 4))) ^
                      (sum ^ next));
            keys[index] = static_cast<uint8_t>(mixed);
            previous = mixed & 0xFF;
        }
        uint32_t last = next;
        next = keys[0];
        last +=
            ((((previous >> 5) ^ (next << 2)) + ((next >> 3) ^ (previous << 4))) ^ (sum ^ next));
        keys[15] = static_cast<uint8_t>(last);
        previous = last & 0xFF;
    }

    for (uint8_t i = 0; i < 4; i++) data[2 + i] ^= get_uint32(keys + 4 * static_cast<ptrdiff_t>(i));
}

void crypt_frame(uint8_t* data, uint32_t utc_s, bool encode) {
    uint32_t words[kWords];
    for (uint8_t i = 0; i < kWords; i++)
        words[i] = get_uint32(data + 4 * static_cast<ptrdiff_t>(i));
    if (encode) {
        scramble(words, utc_s);
        block_tea(words + 2, true);
    } else {
        block_tea(words + 2, false);
        scramble(words, utc_s);
    }
    for (uint8_t i = 0; i < kWords; i++) put_uint32(data + 4 * static_cast<ptrdiff_t>(i), words[i]);
}

// INFO: fc 09mar26 longitude quantum in 1e-7 deg as a function of |latitude| in
// whole degrees: the protocol widens the step towards the poles so a degree of
// longitude keeps roughly constant ground resolution.
int32_t longitude_divisor(int32_t absolute_latitude_deg) {
    static const uint8_t kMidLatitude[65] = {
        53,  53,  54,  54,  55,  55,  56,  56,  57,  57,  58,  58,  59,  59,  60,  60,  61,
        61,  62,  62,  63,  63,  64,  64,  65,  65,  67,  68,  70,  71,  73,  74,  76,  77,
        79,  80,  82,  83,  85,  86,  88,  89,  91,  94,  98,  101, 105, 108, 112, 115, 119,
        122, 126, 129, 137, 144, 152, 159, 167, 174, 190, 205, 221, 236, 252};
    static const uint16_t kPolar[11] = {267, 299, 330, 362, 425, 489, 552, 616, 679, 743, 806};
    if (absolute_latitude_deg < 14) return 52;
    if (absolute_latitude_deg < 79) return kMidLatitude[absolute_latitude_deg - 14];
    if (absolute_latitude_deg > 89) return 806;
    return kPolar[absolute_latitude_deg - 79];
}

uint32_t encode_scaled(int32_t value, uint32_t mantissa_bits, uint32_t exponent_bits,
                       uint32_t sign_bits) {
    uint32_t offset = 1u << mantissa_bits;
    uint32_t sign_bit = offset << exponent_bits;
    uint32_t negative = 0;
    if (value < 0) {
        if (sign_bits == 0) return 0;
        value = -value;
        negative = sign_bit;
    }
    uint32_t magnitude = static_cast<uint32_t>(value);
    if (magnitude < offset) return negative | magnitude;
    uint32_t exponent = 0;
    uint32_t mantissa = offset + magnitude;
    uint32_t mantissa_limit = offset + offset - 1;
    uint32_t exponent_limit = sign_bit - 1;
    while (mantissa > mantissa_limit) {
        mantissa >>= 1;
        exponent += offset;
        if (exponent > exponent_limit) return negative | exponent_limit;
    }
    mantissa -= offset;
    return negative | exponent | mantissa;
}

int32_t decode_scaled(uint32_t value, uint32_t mantissa_bits, uint32_t exponent_bits,
                      uint32_t sign_bits) {
    uint32_t offset = 1u << mantissa_bits;
    uint32_t sign_bit = offset << exponent_bits;
    uint32_t negative = 0;
    if (sign_bits != 0) negative = value & sign_bit;
    value &= (sign_bit - 1);
    if (value >= offset) {
        uint32_t exponent = value >> mantissa_bits;
        value &= (offset - 1);
        value += offset;
        value <<= exponent;
        value -= offset;
    }
    int32_t out = static_cast<int32_t>(value);
    return negative ? -out : out;
}

int32_t unwrap20(uint32_t coded, int32_t reference) {
    int32_t delta = static_cast<int32_t>((coded - static_cast<uint32_t>(reference)) & 0x0FFFFFu);
    if (delta >= 0x080000) delta -= 0x100000;
    return delta + reference;
}

uint16_t carried_crc(const uint8_t* frame) {
    return static_cast<uint16_t>(static_cast<uint16_t>(frame[kAlptasDataBytes] << 8) |
                                 static_cast<uint16_t>(frame[kAlptasDataBytes + 1]));
}

uint16_t crc_syndrome(uint16_t bit) {
    if (bit >= kAlptasDataBytes * 8u)
        return static_cast<uint16_t>(0x8000u >> (bit - kAlptasDataBytes * 8u));
    uint8_t flipped[kAlptasDataBytes] = {0};
    flipped[bit >> 3] = static_cast<uint8_t>(0x80u >> (bit & 7u));
    return fec::crc16_ccitt(flipped, kAlptasDataBytes, 0);
}

bool structured(const uint8_t* decrypted) {
    return get_field(decrypted, kLastByteField) == 0 && get_field(decrypted, kNeedsThreeField) == 3;
}

}  // namespace

uint16_t alptas_crc(const uint8_t* data) {
    return fec::crc16_ccitt(data, kAlptasDataBytes, kAlptasCrcInit);
}

void alptas_set_crc(uint8_t* frame) {
    uint16_t crc = alptas_crc(frame);
    frame[kAlptasDataBytes] = static_cast<uint8_t>(crc >> 8);
    frame[kAlptasDataBytes + 1] = static_cast<uint8_t>(crc);
}

bool alptas_crc_ok(const uint8_t* frame) { return alptas_crc(frame) == carried_crc(frame); }

int alptas_correct(uint8_t* frame, const uint8_t* erasures, int max_erasures) {
    uint16_t check = static_cast<uint16_t>(alptas_crc(frame) ^ carried_crc(frame));
    if (check == 0) return 0;

    static constexpr int kMaxErasures = 8;
    max_erasures = std::min(max_erasures, kMaxErasures);
    uint8_t byte_of[kMaxErasures];
    uint8_t mask[kMaxErasures];
    uint16_t syndrome[kMaxErasures];
    int marked = 0;
    for (int byte = 0; byte < kAlptasFrameBytes; byte++) {
        for (int bit = 0; bit < 8; bit++) {
            const uint8_t bit_mask = static_cast<uint8_t>(0x80u >> bit);
            if ((erasures[byte] & bit_mask) == 0) continue;
            if (marked < max_erasures) {
                byte_of[marked] = static_cast<uint8_t>(byte);
                mask[marked] = bit_mask;
                syndrome[marked] = crc_syndrome(static_cast<uint16_t>(byte * 8 + bit));
            }
            marked++;
        }
        if (marked > max_erasures) return -1;
    }
    if (marked == 0) return -1;

    const int loops = 1 << marked;
    uint8_t previous = 0;
    for (int i = 1; i < loops; i++) {
        const uint8_t gray = static_cast<uint8_t>(i ^ (i >> 1));
        uint8_t moved = static_cast<uint8_t>(gray ^ previous);
        int bit = 0;
        while (moved >>= 1) bit++;
        frame[byte_of[bit]] ^= mask[bit];
        check ^= syndrome[bit];
        previous = gray;
        if (check == 0) return count_ones(gray);
    }
    for (int bit = 0; bit < marked; bit++)
        if (previous & (1u << bit)) frame[byte_of[bit]] ^= mask[bit];
    return -1;
}

Status alptas_keyed_second(const uint8_t* frame, uint32_t rx_utc_s, uint32_t& keyed_utc_s) {
    if (!alptas_crc_ok(frame)) return Status::Crc;
    if (get_field(frame, kMessageTypeField) != kAlptasMessageTypePosition)
        return Status::Unsupported;

    const uint32_t first = (rx_utc_s - kAlptasKeyWindowS) >> 4;
    const uint32_t last = (rx_utc_s + kAlptasKeyWindowS) >> 4;
    for (uint32_t block = first; block <= last; block++) {
        uint8_t data[kAlptasDataBytes];
        __builtin_memcpy(data, frame, kAlptasDataBytes);
        crypt_frame(data, block << 4, false);
        if (!structured(data)) continue;
        const uint32_t second = (block << 4) | get_field(data, kTimeBitsField);
        const int32_t offset = static_cast<int32_t>(second - rx_utc_s);
        if (offset < -kAlptasKeyWindowS || offset > kAlptasKeyWindowS) continue;
        keyed_utc_s = second;
        return Status::Ok;
    }
    return Status::Invalid;
}

uint32_t alptas_address(const uint8_t* frame) { return get_field(frame, kAddressField); }

uint8_t alptas_type_to_adsl_category(uint8_t alptas_type) {
    static const uint8_t kMap[16] = {0, 4, 1, 3, 8, 1, 7, 7, 1, 2, 3, 5, 5, 11, 0, 0};
    return alptas_type < 16 ? kMap[alptas_type] : 0;
}

uint8_t alptas_flight_state(uint8_t alptas_airborne) {
    switch (alptas_airborne) {
        case 1: return static_cast<uint8_t>(flight::FlightState::Ground);
        case 2:
        case 3: return static_cast<uint8_t>(flight::FlightState::Airborne);
        default: return static_cast<uint8_t>(flight::FlightState::Unknown);
    }
}

uint8_t alptas_address_type_to_address_mapping_table(uint8_t alptas_address_type) {
    switch (alptas_address_type & 3) {
        case 1: return 0x05;
        case 2: return 0x06;
        default: return 0;
    }
}

uint8_t address_mapping_table_to_alptas_address_type(uint8_t address_mapping_table) {
    switch (address_mapping_table) {
        case 0x05: return 1;
        case 0x06: return 2;
        default: return 0;
    }
}

Status alptas_decode(const uint8_t* frame, uint32_t rx_utc_s, int32_t reference_latitude_1e7,
                     int32_t reference_longitude_1e7, model::AircraftObservation& out) {
    if (!alptas_crc_ok(frame)) return Status::Crc;

    uint8_t data[kAlptasDataBytes];
    __builtin_memcpy(data, frame, kAlptasDataBytes);
    if (get_field(data, kMessageTypeField) != kAlptasMessageTypePosition)
        return Status::Unsupported;

    crypt_frame(data, rx_utc_s, false);

    // INFO: fc 09mar26 the only decrypt check the protocol offers: the frame's 4
    // time bits must agree with the local second to +/-1 (a rollover where one
    // side is 0 fails, upstream accepts that), the last byte must be 0 and the
    // needs3 nibble must be 3. Without this gate a mis-framed foreign packet
    // decrypts into plausible-looking traffic.
    uint32_t time_bits = get_field(data, kTimeBitsField);
    uint32_t local_bits = rx_utc_s & 0x0Fu;
    bool time_ok =
        (local_bits == time_bits) || (local_bits + 1 == time_bits) || (local_bits == time_bits + 1);
    if (!time_ok) return Status::Invalid;
    if (get_field(data, kLastByteField) != 0 || get_field(data, kNeedsThreeField) != 3)
        return Status::Invalid;

    uint32_t course_deg = get_field(data, kCourseField) >> 1;
    if (course_deg > 360) return Status::Invalid;

    int32_t latitude_reference = divide_rounded<int32_t>(reference_latitude_1e7, 52);
    int32_t latitude_1e7 = unwrap20(get_field(data, kLatitudeField), latitude_reference) * 52;
    if (latitude_1e7 < -900000000 || latitude_1e7 > 900000000) return Status::Invalid;
    int32_t divisor = longitude_divisor(std::abs(latitude_1e7) / 10000000);
    int32_t longitude_reference = divide_rounded<int32_t>(reference_longitude_1e7, divisor);
    int64_t longitude_1e7 =
        static_cast<int64_t>(unwrap20(get_field(data, kLongitudeField), longitude_reference)) *
        divisor;
    if (longitude_1e7 < -1800000000 || longitude_1e7 > 1800000000) return Status::Invalid;

    int32_t speed_e1 = decode_scaled(get_field(data, kSpeedField), 8, 2, 0);
    int32_t climb_e1 = decode_scaled(get_field(data, kClimbField), 6, 2, 1);

    out = model::AircraftObservation{};
    out.address = get_field(data, kAddressField);
    out.address_mapping_table = alptas_address_type_to_address_mapping_table(
        static_cast<uint8_t>(get_field(data, kAddressTypeField)));
    out.aircraft_category =
        alptas_type_to_adsl_category(static_cast<uint8_t>(get_field(data, kAircraftTypeField)));
    out.flight_state = alptas_flight_state(static_cast<uint8_t>(get_field(data, kAirborneField)));
    out.emergency = 1;  // INFO: fc 09mar26 no emergency field on the wire; 1 is ADS-L "OK"
    out.latitude_1e7 = latitude_1e7;
    out.longitude_1e7 = static_cast<int32_t>(longitude_1e7);
    out.altitude_m = decode_scaled(get_field(data, kAltitudeField), 12, 1, 0) - 1000;
    out.speed_q = static_cast<uint16_t>(divide_rounded<int32_t>(speed_e1 * 2, 5));
    out.climb_e8 = static_cast<int16_t>(divide_rounded<int32_t>(climb_e1 * 4, 5));
    out.track_c9 = static_cast<uint16_t>(
        divide_rounded<int32_t>(static_cast<int32_t>(course_deg) * 512, 360) & 0x1FF);
    out.received.at_s = rx_utc_s;
    out.source = model::Source::Alptas;
    out.speed_valid = true;
    out.climb_valid = true;
    out.position_valid = true;
    return Status::Ok;
}

Status alptas_encode(uint8_t* frame, const model::AircraftObservation& observation, uint32_t utc_s,
                     int32_t reference_latitude_1e7, int32_t reference_longitude_1e7) {
    // INFO: fc 09mar26 the reference position is a decode-side input only: the
    // wire carries position modulo 2^20 quanta, absolute, and the receiver
    // reconstructs the high bits from where it is itself (Legacy.cpp:1045).
    (void)reference_latitude_1e7;
    (void)reference_longitude_1e7;
    if (!observation.position_valid) return Status::Invalid;

    __builtin_memset(frame, 0, kAlptasFrameBytes);
    put_field(frame, kAddressField, observation.address & settings::kAddressMask);
    put_field(frame, kMessageTypeField, kAlptasMessageTypePosition);
    put_field(frame, kAddressTypeField,
              address_mapping_table_to_alptas_address_type(observation.address_mapping_table));
    put_field(frame, kTimeBitsField, utc_s & 0x0Fu);
    put_field(frame, kAircraftTypeField,
              adsl_category_to_alptas_type(observation.aircraft_category));
    put_field(frame, kAirborneField, observation.flight_state == 1 ? 1 : 2);
    put_field(frame, kAltitudeField, encode_scaled(observation.altitude_m + 1000, 12, 1, 0));

    int32_t latitude_coded = divide_rounded<int32_t>(observation.latitude_1e7, 52);
    put_field(frame, kLatitudeField, static_cast<uint32_t>(latitude_coded) & 0x0FFFFFu);
    int32_t divisor = longitude_divisor(std::abs(observation.latitude_1e7) / 10000000);
    int32_t longitude_coded = divide_rounded<int32_t>(observation.longitude_1e7, divisor);
    put_field(frame, kLongitudeField, static_cast<uint32_t>(longitude_coded) & 0x0FFFFFu);

    int32_t speed_e1 =
        observation.speed_valid ? divide_rounded<int32_t>(observation.speed_q * 5, 2) : 0;
    put_field(frame, kSpeedField, encode_scaled(speed_e1, 8, 2, 0));
    int32_t climb_e1 =
        observation.climb_valid ? divide_rounded<int32_t>(observation.climb_e8 * 5, 4) : 0;
    put_field(frame, kClimbField, encode_scaled(climb_e1, 6, 2, 1));
    put_field(frame, kTurnRateField, encode_scaled(0, 6, 2, 1));

    uint32_t course_deg =
        static_cast<uint32_t>(divide_rounded<int32_t>(observation.track_c9 * 360, 512)) % 360u;
    put_field(frame, kCourseField, (course_deg << 1) & 0x03FFu);

    // INFO: fc 09mar26 GNSS accuracy and unk8 carry the fixed values upstream
    // transmits (Legacy.cpp:1075-1077): the accuracy fields have a known scaling
    // but no source of truth here, and unk8's meaning is unknown.
    put_field(frame, kGnssAccuracyFirstField, 0x12);
    put_field(frame, kGnssAccuracySecondField, 0x0A);
    put_field(frame, kUnknownField, 11);
    put_field(frame, kNeedsThreeField, 3);
    put_field(frame, kHasThreeField, 3);

    crypt_frame(frame, utc_s, true);
    alptas_set_crc(frame);
    return Status::Ok;
}

}
