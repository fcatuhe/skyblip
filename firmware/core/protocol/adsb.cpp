#include "core/protocol/adsb.h"

#include <cstring>
#include <initializer_list>

#include "core/fec/crc.h"
#include "core/fec/manchester.h"
#include "core/fec/repair.h"

namespace skyblip::protocol {

namespace {

constexpr uint16_t kFrameBits = kAdsbFrameBytes * 8;
constexpr uint8_t kExtendedSquitter = 17;
constexpr uint8_t kMessageFieldOffsetBits = 32;
constexpr int kDoubtfulBitsRepaired = 1;

constexpr auto kParitySyndromes = fec::syndromes<kFrameBits>([](uint16_t bit) {
    uint8_t frame[kAdsbFrameBytes] = {};
    frame[bit >> 3] = static_cast<uint8_t>(0x80u >> (bit & 7));
    return fec::adsl_crc_check(frame, kAdsbFrameBytes);
});

constexpr char kCallsignCharacters[] =
    "?ABCDEFGHIJKLMNOPQRSTUVWXYZ????? ???????????????0123456789??????";

struct MovementBand {
    uint8_t first_code;
    int32_t floor_mkt;
    int32_t step_mkt;
};

// INFO: fc 10nov26 Doc 9871 A.2.3.3.1: codes 125 to 127 are reserved, 124 is 175 kt or more
constexpr MovementBand kMovementBands[] = {
    {1, 0, 0},         {2, 125, 125},     {9, 1000, 250},      {13, 2000, 500},
    {39, 15000, 1000}, {94, 70000, 2000}, {109, 100000, 5000}, {124, 175000, 0},
};
constexpr uint8_t kMovementSaturatedCode = 124;

// INFO: fc 10nov26 DO-260A Appendix A draft, Figure A-6: a vertical rate step is 64 ft/min
constexpr int32_t kClimbStepFpm = 64;
constexpr int32_t kHeightDifferenceStepFt = 25;
constexpr int32_t kSupersonicStepKt = 4;
constexpr int32_t kQAltitudeStepFt = 25;
constexpr int32_t kQAltitudeFloorFt = -1000;
constexpr uint32_t kQBit = 0x010;
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

// INFO: fc 10nov26 1090 sends a 1 as the chips 10, the inverse of the ADS-L table fec decodes
void decode_chips(const uint8_t* chips, uint8_t* frame, uint8_t* erasures) {
    uint8_t inverted[2 * kAdsbFrameBytes];
    inverted[0] = static_cast<uint8_t>(~kAdsbSyncChips);
    for (uint8_t i = 0; i < kAdsbChipBytes; i++) inverted[i + 1] = static_cast<uint8_t>(~chips[i]);
    fec::manchester_decode(inverted, kAdsbFrameBytes, frame, erasures);
}

AdsbQuantity quantity_at(const uint8_t* frame, uint8_t first, uint8_t last, int32_t step) {
    const uint32_t code = message_bits(frame, first, last);
    AdsbQuantity quantity;
    quantity.valid = code != 0;
    quantity.saturated = code == (1u << (last - first + 1)) - 1;
    quantity.value = quantity.valid ? static_cast<int32_t>(code - 1) * step : 0;
    return quantity;
}

AdsbQuantity signed_quantity_at(const uint8_t* frame, uint8_t sign, uint8_t first, uint8_t last,
                                int32_t step) {
    AdsbQuantity quantity = quantity_at(frame, first, last, step);
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

AdsbCpr cpr_of(const uint8_t* frame) {
    AdsbCpr cpr;
    cpr.odd = message_bits(frame, 22, 22) != 0;
    cpr.latitude_cpr = message_bits(frame, 23, 39);
    cpr.longitude_cpr = message_bits(frame, 40, 56);
    return cpr;
}

AdsbIdentification identification_of(const uint8_t* frame, uint8_t type_code) {
    AdsbIdentification identification;
    identification.category_set = static_cast<char>('A' + (4 - type_code));
    identification.category = static_cast<uint8_t>(message_bits(frame, 6, 8));
    bool readable = true;
    for (uint8_t i = 0; i < kAdsbCallsignCharacters; i++) {
        const uint8_t first = static_cast<uint8_t>(9 + 6 * i);
        const char character =
            kCallsignCharacters[message_bits(frame, first, static_cast<uint8_t>(first + 5))];
        readable = readable && character != '?';
        identification.callsign[i] = character;
    }
    size_t length = kAdsbCallsignCharacters;
    while (length > 0 && identification.callsign[length - 1] == ' ') length--;
    identification.callsign_valid = readable && length > 0;
    if (!identification.callsign_valid) length = 0;
    std::memset(identification.callsign + length, 0, sizeof(identification.callsign) - length);
    return identification;
}

AdsbQuantity movement_speed_of(uint32_t code) {
    AdsbQuantity speed;
    if (code == 0 || code > kMovementSaturatedCode) return speed;
    const MovementBand* band = kMovementBands;
    for (const MovementBand& candidate : kMovementBands)
        if (candidate.first_code <= code) band = &candidate;
    speed.valid = true;
    speed.saturated = code == kMovementSaturatedCode;
    speed.value = band->floor_mkt + static_cast<int32_t>(code - band->first_code) * band->step_mkt;
    return speed;
}

AdsbSurfacePosition surface_position_of(const uint8_t* frame) {
    AdsbSurfacePosition position;
    position.speed_mkt = movement_speed_of(message_bits(frame, 6, 12));
    position.track_valid = message_bits(frame, 13, 13) != 0;
    position.track_c7 = static_cast<uint8_t>(message_bits(frame, 14, 20));
    position.utc_synchronized = message_bits(frame, 21, 21) != 0;
    position.cpr = cpr_of(frame);
    return position;
}

AdsbAirbornePosition airborne_position_of(const uint8_t* frame, bool gnss_height) {
    AdsbAirbornePosition position;
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

AdsbVelocity velocity_of(const uint8_t* frame, uint8_t subtype) {
    AdsbVelocity velocity;
    velocity.subtype = subtype;
    const int32_t speed_step = (subtype == 2 || subtype == 4) ? kSupersonicStepKt : 1;
    if (subtype <= 2) {
        velocity.east_kt = signed_quantity_at(frame, 14, 15, 24, speed_step);
        velocity.north_kt = signed_quantity_at(frame, 25, 26, 35, speed_step);
    } else {
        velocity.heading_valid = message_bits(frame, 14, 14) != 0;
        velocity.heading_c10 = static_cast<uint16_t>(message_bits(frame, 15, 24));
        velocity.airspeed_type =
            message_bits(frame, 25, 25) ? AdsbAirspeed::True : AdsbAirspeed::Indicated;
        velocity.airspeed_kt = quantity_at(frame, 26, 35, speed_step);
    }
    velocity.climb_source =
        message_bits(frame, 36, 36) ? AdsbHeightSource::Pressure : AdsbHeightSource::Gnss;
    velocity.climb_fpm = signed_quantity_at(frame, 37, 38, 46, kClimbStepFpm);
    velocity.gnss_minus_pressure_ft =
        signed_quantity_at(frame, 49, 50, 56, kHeightDifferenceStepFt);
    return velocity;
}

AdsbOperationalStatus operational_status_of(const uint8_t* frame, uint8_t subtype) {
    AdsbOperationalStatus status;
    status.subtype = subtype;
    status.version = static_cast<uint8_t>(message_bits(frame, 41, 43));
    status.gva_valid = subtype == 0 && status.version == kVersionWithGva;
    status.gva = status.gva_valid ? static_cast<uint8_t>(message_bits(frame, 49, 50)) : 0;
    return status;
}

Status parse(const uint8_t* frame, AdsbMessage& out) {
    const uint8_t type_code = out.type_code;
    const uint8_t subtype = static_cast<uint8_t>(message_bits(frame, 6, 8));
    if (type_code >= 1 && type_code <= 4) {
        out.kind = AdsbKind::Identification;
        out.identification = identification_of(frame, type_code);
    } else if (type_code >= 5 && type_code <= 8) {
        out.kind = AdsbKind::SurfacePosition;
        out.surface_position = surface_position_of(frame);
    } else if ((type_code >= 9 && type_code <= 18) || (type_code >= 20 && type_code <= 22)) {
        out.kind = AdsbKind::AirbornePosition;
        out.airborne_position = airborne_position_of(frame, type_code >= 20);
    } else if (type_code == 19 && subtype >= 1 && subtype <= 4) {
        out.kind = AdsbKind::Velocity;
        out.velocity = velocity_of(frame, subtype);
    } else if (type_code == 31 && subtype <= 1) {
        out.kind = AdsbKind::OperationalStatus;
        out.operational_status = operational_status_of(frame, subtype);
    } else {
        return Status::Unsupported;
    }
    return Status::Ok;
}

}

Status adsb_receive(const uint8_t* chips, size_t chip_bytes, AdsbReception reception,
                    AdsbMessage& out) {
    out = AdsbMessage{};
    out.reception = reception;
    if (chip_bytes < kAdsbChipBytes) return Status::Invalid;

    uint8_t frame[kAdsbFrameBytes];
    uint8_t erasures[kAdsbFrameBytes];
    decode_chips(chips, frame, erasures);
    const uint32_t residue = fec::adsl_crc_check(frame, kAdsbFrameBytes);
    if (fec::repair(frame, erasures, residue, kParitySyndromes.check(), kDoubtfulBitsRepaired,
                    false) < 0)
        return Status::Crc;

    if (bits(frame, 1, 5) != kExtendedSquitter) return Status::Unsupported;
    out.capability = static_cast<uint8_t>(bits(frame, 6, 8));
    out.address = bits(frame, 9, 32);
    out.type_code = static_cast<uint8_t>(message_bits(frame, 1, 5));
    return parse(frame, out);
}

}
