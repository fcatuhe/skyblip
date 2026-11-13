#include "protocols/alptas/frame.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>

#include "protocols/common/bit_field.h"
#include "protocols/common/crc.h"
#include "protocols/common/exponential.h"
#include "protocols/common/geometry.h"
#include "protocols/common/little_endian.h"
#include "protocols/common/repair.h"
#include "protocols/common/xxtea.h"

namespace skyblip::alptas {

using wire::BitField;
using wire::divide_rounded;
using wire::Exponential;
using wire::get_uint32;
using wire::put_uint32;
using wire::read_bits;
using wire::Status;
using wire::write_bits;

namespace {

constexpr uint8_t kWords = kDataBytes / 4;
constexpr uint16_t kFrameBits = kFrameBytes * 8;
constexpr uint16_t kDataBits = kDataBytes * 8;
constexpr int kMostErasures = 8;
constexpr uint32_t kDelta = 0x9E3779B9u;
constexpr uint32_t kCipherRounds = 6;
constexpr wire::XxteaKey kCipherKey = {0xA5F9B21Cu, 0xAB3F9D12u, 0xC6F34E34u, 0xD72FA378u};
constexpr uint32_t kMaskKey = 0x956F6C77u;

constexpr BitField kAddressField{0, 24};
constexpr BitField kMessageTypeField{24, 4};
constexpr BitField kAddressTypeField{28, 3};
constexpr BitField kPatternLowField{56, 4};
constexpr BitField kPatternHighField{60, 4};
constexpr BitField kTimeBitsField{66, 4};
constexpr BitField kAircraftTypeField{70, 4};
constexpr BitField kAltitudeField{75, 13};
constexpr BitField kLatitudeField{88, 20};
constexpr BitField kLongitudeField{108, 20};
constexpr BitField kTurnRateField{128, 9};
constexpr BitField kSpeedField{137, 10};
constexpr BitField kClimbField{147, 9};
constexpr BitField kCourseField{156, 10};
constexpr BitField kAirborneField{166, 2};
constexpr BitField kGnssAccuracyFirstField{168, 6};
constexpr BitField kGnssAccuracySecondField{174, 5};
constexpr BitField kUnknownField{179, 5};
constexpr BitField kTrailingByteField{184, 8};

constexpr Exponential kAltitudeCode{12, 1, false};
constexpr Exponential kSpeedCode{8, 2, false};
constexpr Exponential kVerticalCode{6, 2, true};

constexpr int32_t kAltitudeOffsetM = 1000;
constexpr int32_t kLatitudeStep1e7 = 52;
constexpr uint32_t kPositionMask = 0x0FFFFFu;

// INFO: fc 19sep26 a carried CRC bit moves the check by itself, a data bit by its CRC from zero
constexpr auto kCrcSyndromes = wire::syndromes<kFrameBits>([](uint16_t bit) -> uint32_t {
    if (bit >= kDataBits) return 0x8000u >> (bit - kDataBits);
    uint8_t data[kDataBytes] = {};
    data[bit >> 3] = static_cast<uint8_t>(0x80u >> (bit & 7));
    return wire::crc16_ccitt(data, kDataBytes, 0);
});

uint32_t mask_mix(uint32_t after, uint32_t before, uint32_t sum) {
    return (((before >> 5) ^ (after << 2)) + ((after >> 3) ^ (before << 4))) ^ (sum ^ after);
}

// INFO: fc 09mar26 the mask keys on the UTC second, so a receiver needs the second it heard
void apply_mask(uint32_t* words, uint32_t utc_s) {
    uint8_t mask[16];
    put_uint32(mask + 0, words[0]);
    put_uint32(mask + 4, words[1]);
    put_uint32(mask + 8, utc_s >> 4);
    put_uint32(mask + 12, kMaskKey);
    uint32_t sum = 0;
    uint32_t before = mask[15];
    for (int round = 0; round < 2; round++) {
        sum += kDelta;
        for (int index = 0; index < 16; index++) {
            const uint32_t after = mask[(index + 1) % 16];
            mask[index] = static_cast<uint8_t>(mask[index] + mask_mix(after, before, sum));
            before = mask[index];
        }
    }
    for (ptrdiff_t word = 0; word < 4; word++) words[2 + word] ^= get_uint32(mask + 4 * word);
}

void crypt_frame(uint8_t* data, uint32_t utc_s, bool encrypt) {
    uint32_t words[kWords];
    for (ptrdiff_t word = 0; word < kWords; word++) words[word] = get_uint32(data + 4 * word);
    if (encrypt) {
        apply_mask(words, utc_s);
        wire::xxtea_encrypt(words + 2, kWords - 2, kCipherKey, kCipherRounds);
    } else {
        wire::xxtea_decrypt(words + 2, kWords - 2, kCipherKey, kCipherRounds);
        apply_mask(words, utc_s);
    }
    for (ptrdiff_t word = 0; word < kWords; word++) put_uint32(data + 4 * word, words[word]);
}

// INFO: fc 09mar26 the longitude step widens towards the poles to keep its ground length
int32_t longitude_step_1e7(int32_t absolute_latitude_deg) {
    static const uint16_t kStepFrom14Degrees[76] = {
        53,  53,  54,  54,  55,  55,  56,  56,  57,  57,  58,  58,  59,  59,  60,  60,
        61,  61,  62,  62,  63,  63,  64,  64,  65,  65,  67,  68,  70,  71,  73,  74,
        76,  77,  79,  80,  82,  83,  85,  86,  88,  89,  91,  94,  98,  101, 105, 108,
        112, 115, 119, 122, 126, 129, 137, 144, 152, 159, 167, 174, 190, 205, 221, 236,
        252, 267, 299, 330, 362, 425, 489, 552, 616, 679, 743, 806};
    if (absolute_latitude_deg < 14) return 52;
    if (absolute_latitude_deg > 89) return 806;
    return kStepFrom14Degrees[absolute_latitude_deg - 14];
}

int32_t nearest_with_low_bits(uint32_t low_bits, int32_t reference) {
    const uint32_t ahead = (low_bits - static_cast<uint32_t>(reference)) & kPositionMask;
    const int32_t half = static_cast<int32_t>((kPositionMask + 1) / 2);
    return reference + static_cast<int32_t>(ahead ^ static_cast<uint32_t>(half)) - half;
}

uint16_t carried_crc(const uint8_t* frame) {
    return static_cast<uint16_t>((frame[kDataBytes] << 8) | frame[kDataBytes + 1]);
}

bool structured(const uint8_t* decrypted) {
    return read_bits(decrypted, kTrailingByteField) == 0 &&
           read_bits(decrypted, kPatternLowField) == 3;
}

// INFO: fc 09mar26 the time bits match to within one second, so a 15 against a 0 fails
bool keyed_on(const uint8_t* decrypted, uint32_t rx_utc_s) {
    const int32_t sent = static_cast<int32_t>(read_bits(decrypted, kTimeBitsField));
    const int32_t here = static_cast<int32_t>(rx_utc_s & 0x0Fu);
    return std::abs(sent - here) <= 1;
}

}  // namespace

uint16_t crc(const uint8_t* data) { return wire::crc16_ccitt(data, kDataBytes, kCrcInit); }

void set_crc(uint8_t* frame) {
    const uint16_t check = crc(frame);
    frame[kDataBytes] = static_cast<uint8_t>(check >> 8);
    frame[kDataBytes + 1] = static_cast<uint8_t>(check);
}

bool crc_ok(const uint8_t* frame) { return crc(frame) == carried_crc(frame); }

int correct(uint8_t* frame, const uint8_t* erasures, int max_erasures) {
    const uint32_t residue = static_cast<uint32_t>(crc(frame) ^ carried_crc(frame));
    return wire::repair(frame, erasures, residue, kCrcSyndromes.check(),
                        std::min(max_erasures, kMostErasures), false);
}

Status keyed_second(const uint8_t* frame, uint32_t rx_utc_s, uint32_t& keyed_utc_s) {
    if (!crc_ok(frame)) return Status::Crc;
    if (read_bits(frame, kMessageTypeField) != kMessageTypePosition) return Status::Unsupported;

    const uint32_t first = (rx_utc_s - kAlptasKeyWindowS) >> 4;
    const uint32_t last = (rx_utc_s + kAlptasKeyWindowS) >> 4;
    for (uint32_t block = first; block <= last; block++) {
        uint8_t data[kDataBytes];
        __builtin_memcpy(data, frame, kDataBytes);
        crypt_frame(data, block << 4, false);
        if (!structured(data)) continue;
        const uint32_t second = (block << 4) | read_bits(data, kTimeBitsField);
        const int32_t offset = static_cast<int32_t>(second - rx_utc_s);
        if (offset < -kAlptasKeyWindowS || offset > kAlptasKeyWindowS) continue;
        keyed_utc_s = second;
        return Status::Ok;
    }
    return Status::Invalid;
}

uint32_t address(const uint8_t* frame) { return read_bits(frame, kAddressField); }

Status decode(const uint8_t* frame, uint32_t rx_utc_s, int32_t reference_latitude_1e7,
              int32_t reference_longitude_1e7, Position& out) {
    if (!crc_ok(frame)) return Status::Crc;
    if (read_bits(frame, kMessageTypeField) != kMessageTypePosition) return Status::Unsupported;

    uint8_t data[kDataBytes];
    __builtin_memcpy(data, frame, kDataBytes);
    crypt_frame(data, rx_utc_s, false);
    // INFO: fc 09mar26 without this gate a mis-framed foreign packet decodes as traffic
    if (!keyed_on(data, rx_utc_s) || !structured(data)) return Status::Invalid;

    const uint32_t course_deg = read_bits(data, kCourseField) >> 1;
    if (course_deg > 360) return Status::Invalid;

    const int32_t latitude_1e7 =
        nearest_with_low_bits(read_bits(data, kLatitudeField),
                              divide_rounded<int32_t>(reference_latitude_1e7, kLatitudeStep1e7)) *
        kLatitudeStep1e7;
    if (latitude_1e7 < -900000000 || latitude_1e7 > 900000000) return Status::Invalid;
    const int32_t longitude_step = longitude_step_1e7(std::abs(latitude_1e7) / 10000000);
    const int64_t longitude_1e7 =
        static_cast<int64_t>(nearest_with_low_bits(
            read_bits(data, kLongitudeField),
            divide_rounded<int32_t>(reference_longitude_1e7, longitude_step))) *
        longitude_step;
    if (longitude_1e7 < -1800000000 || longitude_1e7 > 1800000000) return Status::Invalid;

    out = Position{};
    out.address = read_bits(data, kAddressField);
    out.address_type = static_cast<uint8_t>(read_bits(data, kAddressTypeField));
    out.aircraft_type = static_cast<uint8_t>(read_bits(data, kAircraftTypeField));
    out.airborne = static_cast<uint8_t>(read_bits(data, kAirborneField));
    out.latitude_1e7 = latitude_1e7;
    out.longitude_1e7 = static_cast<int32_t>(longitude_1e7);
    out.altitude_m = kAltitudeCode.decode(read_bits(data, kAltitudeField)) - kAltitudeOffsetM;
    out.speed_e1 = kSpeedCode.decode(read_bits(data, kSpeedField));
    out.climb_e1 = kVerticalCode.decode(read_bits(data, kClimbField));
    out.course_deg = static_cast<uint16_t>(course_deg);
    return Status::Ok;
}

// INFO: fc 09mar26 position goes out modulo 2^20 steps, so only the decoder needs a reference
void encode(uint8_t* frame, const Position& position, uint32_t utc_s) {
    __builtin_memset(frame, 0, kFrameBytes);
    write_bits(frame, kAddressField, position.address);
    write_bits(frame, kMessageTypeField, kMessageTypePosition);
    write_bits(frame, kAddressTypeField, position.address_type);
    write_bits(frame, kTimeBitsField, utc_s & 0x0Fu);
    write_bits(frame, kAircraftTypeField, position.aircraft_type);
    write_bits(frame, kAirborneField, position.airborne);
    write_bits(frame, kAltitudeField, kAltitudeCode.encode(position.altitude_m + kAltitudeOffsetM));

    const int32_t latitude_steps = divide_rounded<int32_t>(position.latitude_1e7, kLatitudeStep1e7);
    write_bits(frame, kLatitudeField, static_cast<uint32_t>(latitude_steps) & kPositionMask);
    const int32_t longitude_step = longitude_step_1e7(std::abs(position.latitude_1e7) / 10000000);
    const int32_t longitude_steps = divide_rounded<int32_t>(position.longitude_1e7, longitude_step);
    write_bits(frame, kLongitudeField, static_cast<uint32_t>(longitude_steps) & kPositionMask);

    write_bits(frame, kSpeedField, kSpeedCode.encode(position.speed_e1));
    write_bits(frame, kClimbField, kVerticalCode.encode(position.climb_e1));
    write_bits(frame, kTurnRateField, kVerticalCode.encode(0));
    write_bits(frame, kCourseField, (static_cast<uint32_t>(position.course_deg) << 1) & 0x03FFu);

    // INFO: fc 09mar26 accuracy and unknown fields carry the constants SoftRF latest_encode sends
    write_bits(frame, kGnssAccuracyFirstField, 0x12);
    write_bits(frame, kGnssAccuracySecondField, 0x0A);
    write_bits(frame, kUnknownField, 11);
    write_bits(frame, kPatternLowField, 3);
    write_bits(frame, kPatternHighField, 3);

    crypt_frame(frame, utc_s, true);
    set_crc(frame);
}

}
