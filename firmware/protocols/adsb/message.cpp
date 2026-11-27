#include "protocols/adsb/message.h"

#include <array>
#include <cstring>
#include <initializer_list>

#include "protocols/common/crc.h"

namespace skyblip::adsb {

namespace {

constexpr uint8_t kExtendedSquitter = 17;
constexpr uint8_t kAllCallReply = 11;
constexpr uint8_t kShortAirAir = 0;
constexpr uint8_t kAltitudeReply = 4;
constexpr uint8_t kIdentityReply = 5;
constexpr uint8_t kLongAirAir = 16;
constexpr uint8_t kCommBAltitudeReply = 20;
constexpr uint8_t kCommBIdentityReply = 21;
// INFO: fc 18nov26 an all-call reply to an interrogator code leaves it in the parity's low 7 bits
constexpr uint32_t kInterrogatorCodeCeiling = 0x7F;
constexpr uint8_t kMessageFieldOffsetBits = 32;

constexpr char kCharacterSet[] = "?ABCDEFGHIJKLMNOPQRSTUVWXYZ????? ???????????????0123456789??????";

// INFO: fc 10nov26 DO-260A Appendix A draft, Figure A-6: a vertical rate step is 64 ft/min
constexpr int32_t kClimbStepFpm = 64;
constexpr int32_t kHeightDifferenceStepFt = 25;
constexpr int32_t kSupersonicStepKt = 4;
constexpr int32_t kQAltitudeStepFt = 25;
constexpr int32_t kQAltitudeFloorFt = -1000;
constexpr uint32_t kQBit = 0x010;
constexpr uint32_t kMBit = 0x040;
constexpr uint8_t kVersionWithGva = 2;

uint32_t bits(const uint8_t* frame, uint8_t first, uint8_t last) {
    uint32_t value = 0;
    for (uint8_t bit = first; bit <= last; bit++) {
        const uint8_t index = static_cast<uint8_t>(bit - 1);
        value = (value << 1) | ((frame[index >> 3] >> (7 - (index & 7))) & 1u);
    }
    return value;
}

uint32_t message_bits(const uint8_t* frame, uint8_t first, uint8_t last) {
    return bits(frame, static_cast<uint8_t>(kMessageFieldOffsetBits + first),
                static_cast<uint8_t>(kMessageFieldOffsetBits + last));
}

uint32_t gray_to_binary(uint32_t gray) {
    for (uint32_t shifted = gray >> 1; shifted != 0; shifted >>= 1) gray ^= shifted;
    return gray;
}

Quantity quantity_at(const uint8_t* frame, uint8_t first, uint8_t last, int32_t step) {
    const uint32_t code = message_bits(frame, first, last);
    Quantity quantity;
    quantity.valid = code != 0;
    quantity.saturated = code == (1u << (last - first + 1)) - 1;
    quantity.value = quantity.valid ? static_cast<int32_t>(code - 1) * step : 0;
    return quantity;
}

Quantity signed_quantity_at(const uint8_t* frame, uint8_t sign, uint8_t first, uint8_t last,
                            int32_t step) {
    Quantity quantity = quantity_at(frame, first, last, step);
    if (message_bits(frame, sign, sign)) quantity.value = -quantity.value;
    return quantity;
}

bool gillham_ft_of(uint32_t field, int32_t& feet) {
    enum Position { C1, A1, C2, A2, C4, A4, B1, D1, B2, D2, B4, D4 };
    auto gray_of = [field](std::initializer_list<int> positions) {
        uint32_t gray = 0;
        for (int position : positions) gray = (gray << 1) | ((field >> (11 - position)) & 1u);
        return gray;
    };
    const uint32_t five_hundreds = gray_to_binary(gray_of({D2, D4, A1, A2, A4, B1, B2, B4}));
    uint32_t hundreds = gray_to_binary(gray_of({C1, C2, C4}));
    if (hundreds == 0 || hundreds == 5 || hundreds == 6) return false;
    if (hundreds == 7) hundreds = 5;
    if (five_hundreds & 1u) hundreds = 6 - hundreds;
    feet = static_cast<int32_t>(500 * five_hundreds + 100 * hundreds) - 1300;
    return true;
}

bool altitude_ft_of(uint32_t field, int32_t& feet) {
    if (field == 0) return false;
    if ((field & kQBit) == 0) return gillham_ft_of(field, feet);
    const uint32_t steps = ((field >> 5) << 4) | (field & 0x00Fu);
    feet = static_cast<int32_t>(steps) * kQAltitudeStepFt + kQAltitudeFloorFt;
    return true;
}

// INFO: fc 18nov26 the AC field is the ES altitude with an M bit after A4, and M set is metres
bool altitude_code_ft_of(uint32_t field, int32_t& feet) {
    if (field & kMBit) return false;
    return altitude_ft_of(((field >> 7) << 6) | (field & 0x3Fu), feet);
}

Cpr cpr_of(const uint8_t* frame) {
    Cpr cpr;
    cpr.odd = message_bits(frame, 22, 22) != 0;
    cpr.latitude_cpr = message_bits(frame, 23, 39);
    cpr.longitude_cpr = message_bits(frame, 40, 56);
    return cpr;
}

Identification identification_of(const uint8_t* frame, uint8_t type_code) {
    Identification identification;
    identification.category_set = static_cast<char>('A' + (4 - type_code));
    identification.category = static_cast<uint8_t>(message_bits(frame, 6, 8));
    bool readable = true;
    for (uint8_t i = 0; i < kCallsignCharacters; i++) {
        const uint8_t first = static_cast<uint8_t>(9 + 6 * i);
        const char character =
            kCharacterSet[message_bits(frame, first, static_cast<uint8_t>(first + 5))];
        readable = readable && character != '?';
        identification.callsign[i] = character;
    }
    size_t length = kCallsignCharacters;
    while (length > 0 && identification.callsign[length - 1] == ' ') length--;
    identification.callsign_valid = readable && length > 0;
    if (!identification.callsign_valid) length = 0;
    std::memset(identification.callsign + length, 0, sizeof(identification.callsign) - length);
    return identification;
}

Quantity movement_speed_of(uint32_t code) {
    Quantity speed;
    if (code == 0 || code > kMovementSaturatedCode) return speed;
    const MovementBand* band = kMovementBands;
    for (const MovementBand& candidate : kMovementBands)
        if (candidate.first_code <= code) band = &candidate;
    speed.valid = true;
    speed.saturated = code == kMovementSaturatedCode;
    speed.value = band->floor_mkt + static_cast<int32_t>(code - band->first_code) * band->step_mkt;
    return speed;
}

SurfacePosition surface_position_of(const uint8_t* frame) {
    SurfacePosition position;
    position.speed_mkt = movement_speed_of(message_bits(frame, 6, 12));
    position.track_valid = message_bits(frame, 13, 13) != 0;
    position.track_c7 = static_cast<uint8_t>(message_bits(frame, 14, 20));
    position.utc_synchronized = message_bits(frame, 21, 21) != 0;
    position.cpr = cpr_of(frame);
    return position;
}

AirbornePosition airborne_position_of(const uint8_t* frame, bool gnss_height) {
    AirbornePosition position;
    int32_t feet = 0;
    const bool valid = altitude_ft_of(message_bits(frame, 9, 20), feet);
    if (gnss_height) {
        position.gnss_height_ft = feet;
        position.gnss_height_valid = valid;
    } else {
        position.pressure_altitude_ft = feet;
        position.pressure_altitude_valid = valid;
    }
    position.utc_synchronized = message_bits(frame, 21, 21) != 0;
    position.cpr = cpr_of(frame);
    return position;
}

Velocity velocity_of(const uint8_t* frame, uint8_t subtype) {
    Velocity velocity;
    velocity.subtype = subtype;
    const int32_t speed_step = (subtype == 2 || subtype == 4) ? kSupersonicStepKt : 1;
    if (subtype <= 2) {
        velocity.east_kt = signed_quantity_at(frame, 14, 15, 24, speed_step);
        velocity.north_kt = signed_quantity_at(frame, 25, 26, 35, speed_step);
    } else {
        velocity.heading_valid = message_bits(frame, 14, 14) != 0;
        velocity.heading_c10 = static_cast<uint16_t>(message_bits(frame, 15, 24));
        velocity.airspeed_type = message_bits(frame, 25, 25) ? Airspeed::True : Airspeed::Indicated;
        velocity.airspeed_kt = quantity_at(frame, 26, 35, speed_step);
    }
    velocity.climb_source =
        message_bits(frame, 36, 36) ? HeightSource::Pressure : HeightSource::Gnss;
    velocity.climb_fpm = signed_quantity_at(frame, 37, 38, 46, kClimbStepFpm);
    velocity.gnss_minus_pressure_ft =
        signed_quantity_at(frame, 49, 50, 56, kHeightDifferenceStepFt);
    return velocity;
}

OperationalStatus operational_status_of(const uint8_t* frame, uint8_t subtype) {
    OperationalStatus status;
    status.subtype = subtype;
    status.version = static_cast<uint8_t>(message_bits(frame, 41, 43));
    status.gva_valid = subtype == 0 && status.version == kVersionWithGva;
    status.gva = status.gva_valid ? static_cast<uint8_t>(message_bits(frame, 49, 50)) : 0;
    return status;
}

wire::Status parse(const uint8_t* frame, Message& out) {
    const uint8_t type_code = out.type_code;
    const uint8_t subtype = static_cast<uint8_t>(message_bits(frame, 6, 8));
    if (type_code >= 1 && type_code <= 4) {
        out.kind = Kind::Identification;
        out.identification = identification_of(frame, type_code);
    } else if (type_code >= 5 && type_code <= 8) {
        out.kind = Kind::SurfacePosition;
        out.surface_position = surface_position_of(frame);
    } else if ((type_code >= 9 && type_code <= 18) || (type_code >= 20 && type_code <= 22)) {
        out.kind = Kind::AirbornePosition;
        out.airborne_position = airborne_position_of(frame, type_code >= 20);
    } else if (type_code == 19 && subtype >= 1 && subtype <= 4) {
        out.kind = Kind::Velocity;
        out.velocity = velocity_of(frame, subtype);
    } else if (type_code == 31 && subtype <= 1) {
        out.kind = Kind::OperationalStatus;
        out.operational_status = operational_status_of(frame, subtype);
    } else {
        return wire::Status::Unsupported;
    }
    return wire::Status::Ok;
}

wire::Status extended_squitter(const uint8_t* frame, Message& out) {
    if (wire::crc24_check(frame, kFrameBytes) != 0) return wire::Status::Crc;
    out.capability = static_cast<uint8_t>(bits(frame, 6, 8));
    out.address = bits(frame, 9, 32);
    out.address_verified = true;
    out.type_code = static_cast<uint8_t>(message_bits(frame, 1, 5));
    return parse(frame, out);
}

wire::Status all_call(const uint8_t* frame, Message& out) {
    const uint32_t interrogator = wire::crc24_check(frame, kShortFrameBytes);
    if (interrogator > kInterrogatorCodeCeiling) return wire::Status::Crc;
    out.kind = Kind::AllCall;
    out.capability = static_cast<uint8_t>(bits(frame, 6, 8));
    out.address = bits(frame, 9, 32);
    out.address_verified = interrogator == 0;
    return wire::Status::Ok;
}

wire::Status reply(const uint8_t* frame, Message& out, bool altitude) {
    const size_t bytes = out.downlink_format >= kLongAirAir ? kFrameBytes : kShortFrameBytes;
    out.kind = Kind::Reply;
    out.address = wire::crc24_check(frame, bytes);
    if (altitude)
        out.reply.pressure_altitude_valid =
            altitude_code_ft_of(bits(frame, 20, 32), out.reply.pressure_altitude_ft);
    return wire::Status::Ok;
}

// INFO: fc 27nov26 DO-260B 2.2.4.3.4.7.3.a never corrects the DF field, so it is left out
constexpr uint8_t kFirstCorrectableBit = 6;
constexpr size_t kCorrectableBits = kFrameBytes * 8u - (kFirstCorrectableBit - 1u);

constexpr std::array<uint32_t, kCorrectableBits> one_bit_syndromes() {
    std::array<uint32_t, kCorrectableBits> syndromes{};
    for (size_t i = 0; i < kCorrectableBits; i++) {
        uint8_t error[kFrameBytes]{};
        const size_t index = kFirstCorrectableBit - 1u + i;
        error[index >> 3] = static_cast<uint8_t>(0x80u >> (index & 7u));
        syndromes[i] = wire::crc24_check(error, kFrameBytes);
    }
    return syndromes;
}

struct Syndrome {
    uint32_t value;
    uint8_t bit;
};

// INFO: fc 27nov26 sorted at compile time, so a two-bit search is 107 binary searches, about 10 us
constexpr std::array<Syndrome, kCorrectableBits> sorted_syndromes() {
    const std::array<uint32_t, kCorrectableBits> values = one_bit_syndromes();
    std::array<Syndrome, kCorrectableBits> sorted{};
    for (size_t i = 0; i < kCorrectableBits; i++) {
        size_t at = i;
        while (at > 0 && sorted[at - 1].value > values[i]) {
            sorted[at] = sorted[at - 1];
            at--;
        }
        sorted[at] = {values[i], static_cast<uint8_t>(kFirstCorrectableBit - 1u + i)};
    }
    return sorted;
}

constexpr std::array<Syndrome, kCorrectableBits> kSyndromes = sorted_syndromes();

const Syndrome* find_syndrome(uint32_t value) {
    size_t low = 0;
    size_t high = kCorrectableBits;
    while (low < high) {
        const size_t middle = (low + high) / 2;
        if (kSyndromes[middle].value < value) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low < kCorrectableBits && kSyndromes[low].value == value ? &kSyndromes[low] : nullptr;
}

void flip(uint8_t* frame, uint8_t index) {
    frame[index >> 3] = static_cast<uint8_t>(frame[index >> 3] ^ (0x80u >> (index & 7u)));
}

}

wire::Status receive(const uint8_t* frame, size_t frame_bytes, Reception reception, Message& out) {
    out = Message{};
    out.reception = reception;
    if (frame_bytes < kFrameBytes) return wire::Status::Invalid;

    out.downlink_format = static_cast<uint8_t>(bits(frame, 1, 5));
    switch (out.downlink_format) {
        case kExtendedSquitter: return extended_squitter(frame, out);
        case kAllCallReply: return all_call(frame, out);
        case kShortAirAir:
        case kAltitudeReply:
        case kLongAirAir:
        case kCommBAltitudeReply: return reply(frame, out, true);
        case kIdentityReply:
        case kCommBIdentityReply: return reply(frame, out, false);
        default: return wire::Status::Unsupported;
    }
}

Screened screen(const uint8_t* frame) {
    switch (bits(frame, 1, 5)) {
        case kExtendedSquitter:
            return wire::crc24_check(frame, kFrameBytes) == 0 ? Screened::Squitter
                                                              : Screened::Parity;
        case kAllCallReply:
            return wire::crc24_check(frame, kShortFrameBytes) <= kInterrogatorCodeCeiling
                       ? Screened::AllCall
                       : Screened::Parity;
        case kShortAirAir:
        case kAltitudeReply:
        case kIdentityReply:
        case kLongAirAir:
        case kCommBAltitudeReply:
        case kCommBIdentityReply: return Screened::Reply;
        default: return Screened::Format;
    }
}

// INFO: fc 27nov26 parity is linear: flipped bits in a sound frame leave the XOR of their syndromes
uint8_t repair_bits(uint8_t* frame) {
    if (bits(frame, 1, 5) != kExtendedSquitter) return 0;
    const uint32_t syndrome = wire::crc24_check(frame, kFrameBytes);
    if (syndrome == 0) return 0;
    if (const Syndrome* one = find_syndrome(syndrome)) {
        flip(frame, one->bit);
        return 1;
    }
    for (const Syndrome& first : kSyndromes) {
        const Syndrome* second = find_syndrome(syndrome ^ first.value);
        if (second == nullptr) continue;
        flip(frame, first.bit);
        flip(frame, second->bit);
        return 2;
    }
    return 0;
}

}
