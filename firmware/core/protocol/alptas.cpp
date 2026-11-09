#include "core/protocol/alptas.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>

#include "core/fec/crc.h"
#include "core/fec/repair.h"
#include "core/fec/xxtea.h"
#include "core/flight/state.h"
#include "core/model/aircraft.h"
#include "core/protocol/nmea_out.h"
#include "core/settings/address.h"
#include "core/util/bit_field.h"
#include "core/util/exponential.h"
#include "core/util/intmath.h"
#include "core/util/little_endian.h"

namespace skyblip::protocol {

namespace {

constexpr uint8_t kWords = kAlptasDataBytes / 4;
constexpr uint16_t kFrameBits = kAlptasFrameBytes * 8;
constexpr uint16_t kDataBits = kAlptasDataBytes * 8;
constexpr int kMostErasures = 8;
constexpr uint32_t kDelta = 0x9E3779B9u;
constexpr uint32_t kCipherRounds = 6;
constexpr fec::XxteaKey kCipherKey = {0xA5F9B21Cu, 0xAB3F9D12u, 0xC6F34E34u, 0xD72FA378u};
constexpr uint32_t kMaskKey = 0x956F6C77u;

constexpr BitField kAddressField{0, 24};
constexpr BitField kMessageTypeField{24, 4};
constexpr BitField kAddressTypeField{28, 3};
constexpr BitField kNeedsThreeField{56, 4};
constexpr BitField kHasThreeField{60, 4};
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
constexpr BitField kLastByteField{184, 8};

constexpr Exponential kAltitudeCode{12, 1, false};
constexpr Exponential kSpeedCode{8, 2, false};
constexpr Exponential kVerticalCode{6, 2, true};

constexpr int32_t kAltitudeOffsetM = 1000;
constexpr int32_t kLatitudeStep1e7 = 52;
constexpr uint32_t kPositionMask = 0x0FFFFFu;

// INFO: fc 19sep26 a carried CRC bit moves the check by itself, a data bit by its CRC from zero
constexpr auto kCrcSyndromes = fec::syndromes<kFrameBits>([](uint16_t bit) -> uint32_t {
    if (bit >= kDataBits) return 0x8000u >> (bit - kDataBits);
    uint8_t data[kAlptasDataBytes] = {};
    data[bit >> 3] = static_cast<uint8_t>(0x80u >> (bit & 7));
    return fec::crc16_ccitt(data, kAlptasDataBytes, 0);
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
        fec::xxtea_encrypt(words + 2, kWords - 2, kCipherKey, kCipherRounds);
    } else {
        fec::xxtea_decrypt(words + 2, kWords - 2, kCipherKey, kCipherRounds);
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
    return static_cast<uint16_t>((frame[kAlptasDataBytes] << 8) | frame[kAlptasDataBytes + 1]);
}

bool structured(const uint8_t* decrypted) {
    return read_bits(decrypted, kLastByteField) == 0 && read_bits(decrypted, kNeedsThreeField) == 3;
}

// INFO: fc 09mar26 the time bits match to within one second, so a 15 against a 0 fails
bool keyed_on(const uint8_t* decrypted, uint32_t rx_utc_s) {
    const int32_t sent = static_cast<int32_t>(read_bits(decrypted, kTimeBitsField));
    const int32_t here = static_cast<int32_t>(rx_utc_s & 0x0Fu);
    return std::abs(sent - here) <= 1;
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
    const uint32_t residue = static_cast<uint32_t>(alptas_crc(frame) ^ carried_crc(frame));
    return fec::repair(frame, erasures, residue, kCrcSyndromes.check(),
                       std::min(max_erasures, kMostErasures), false);
}

Status alptas_keyed_second(const uint8_t* frame, uint32_t rx_utc_s, uint32_t& keyed_utc_s) {
    if (!alptas_crc_ok(frame)) return Status::Crc;
    if (read_bits(frame, kMessageTypeField) != kAlptasMessageTypePosition)
        return Status::Unsupported;

    const uint32_t first = (rx_utc_s - kAlptasKeyWindowS) >> 4;
    const uint32_t last = (rx_utc_s + kAlptasKeyWindowS) >> 4;
    for (uint32_t block = first; block <= last; block++) {
        uint8_t data[kAlptasDataBytes];
        __builtin_memcpy(data, frame, kAlptasDataBytes);
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

uint32_t alptas_address(const uint8_t* frame) { return read_bits(frame, kAddressField); }

// INFO: fc 09nov26 FLARM ICD AcftType 0x0 to 0xF in order, where 0x0, 0xA and 0xE name no type
uint8_t alptas_type_to_adsl_category(uint8_t alptas_type) {
    static const uint8_t kCategoryOfType[16] = {0, 4, 1, 3, 8, 1, 7, 7, 1, 2, 0, 5, 5, 11, 0, 0};
    return alptas_type < 16 ? kCategoryOfType[alptas_type] : 0;
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
    if (read_bits(frame, kMessageTypeField) != kAlptasMessageTypePosition)
        return Status::Unsupported;

    uint8_t data[kAlptasDataBytes];
    __builtin_memcpy(data, frame, kAlptasDataBytes);
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

    const int32_t speed_e1 = kSpeedCode.decode(read_bits(data, kSpeedField));
    const int32_t climb_e1 = kVerticalCode.decode(read_bits(data, kClimbField));

    out = model::AircraftObservation{};
    out.address = read_bits(data, kAddressField);
    out.address_mapping_table = alptas_address_type_to_address_mapping_table(
        static_cast<uint8_t>(read_bits(data, kAddressTypeField)));
    out.aircraft_category =
        alptas_type_to_adsl_category(static_cast<uint8_t>(read_bits(data, kAircraftTypeField)));
    out.flight_state = alptas_flight_state(static_cast<uint8_t>(read_bits(data, kAirborneField)));
    out.emergency = 1;  // INFO: fc 09mar26 no emergency field on the wire; 1 is ADS-L "OK"
    out.latitude_1e7 = latitude_1e7;
    out.longitude_1e7 = static_cast<int32_t>(longitude_1e7);
    out.altitude_m = kAltitudeCode.decode(read_bits(data, kAltitudeField)) - kAltitudeOffsetM;
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

// INFO: fc 09mar26 position goes out modulo 2^20 steps, so only the decoder needs a reference
Status alptas_encode(uint8_t* frame, const model::AircraftObservation& observation, uint32_t utc_s,
                     int32_t reference_latitude_1e7, int32_t reference_longitude_1e7) {
    (void)reference_latitude_1e7;
    (void)reference_longitude_1e7;
    if (!observation.position_valid) return Status::Invalid;

    __builtin_memset(frame, 0, kAlptasFrameBytes);
    write_bits(frame, kAddressField, observation.address & settings::kAddressMask);
    write_bits(frame, kMessageTypeField, kAlptasMessageTypePosition);
    write_bits(frame, kAddressTypeField,
               address_mapping_table_to_alptas_address_type(observation.address_mapping_table));
    write_bits(frame, kTimeBitsField, utc_s & 0x0Fu);
    write_bits(frame, kAircraftTypeField,
               adsl_category_to_alptas_type(observation.aircraft_category));
    write_bits(frame, kAirborneField, observation.flight_state == 1 ? 1 : 2);
    write_bits(frame, kAltitudeField,
               kAltitudeCode.encode(observation.altitude_m + kAltitudeOffsetM));

    const int32_t latitude_steps =
        divide_rounded<int32_t>(observation.latitude_1e7, kLatitudeStep1e7);
    write_bits(frame, kLatitudeField, static_cast<uint32_t>(latitude_steps) & kPositionMask);
    const int32_t longitude_step =
        longitude_step_1e7(std::abs(observation.latitude_1e7) / 10000000);
    const int32_t longitude_steps =
        divide_rounded<int32_t>(observation.longitude_1e7, longitude_step);
    write_bits(frame, kLongitudeField, static_cast<uint32_t>(longitude_steps) & kPositionMask);

    const int32_t speed_e1 =
        observation.speed_valid ? divide_rounded<int32_t>(observation.speed_q * 5, 2) : 0;
    write_bits(frame, kSpeedField, kSpeedCode.encode(speed_e1));
    const int32_t climb_e1 =
        observation.climb_valid ? divide_rounded<int32_t>(observation.climb_e8 * 5, 4) : 0;
    write_bits(frame, kClimbField, kVerticalCode.encode(climb_e1));
    write_bits(frame, kTurnRateField, kVerticalCode.encode(0));

    const uint32_t course_deg =
        static_cast<uint32_t>(divide_rounded<int32_t>(observation.track_c9 * 360, 512)) % 360u;
    write_bits(frame, kCourseField, (course_deg << 1) & 0x03FFu);

    // INFO: fc 09mar26 the accuracy fields and unknown bits carry what deployed senders send
    write_bits(frame, kGnssAccuracyFirstField, 0x12);
    write_bits(frame, kGnssAccuracySecondField, 0x0A);
    write_bits(frame, kUnknownField, 11);
    write_bits(frame, kNeedsThreeField, 3);
    write_bits(frame, kHasThreeField, 3);

    crypt_frame(frame, utc_s, true);
    alptas_set_crc(frame);
    return Status::Ok;
}

}
