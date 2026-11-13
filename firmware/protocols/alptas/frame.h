#ifndef SKYBLIP_PROTOCOLS_ALPTAS_FRAME_H
#define SKYBLIP_PROTOCOLS_ALPTAS_FRAME_H

#include <cstdint>

#include "protocols/common/status.h"

namespace skyblip::alptas {

constexpr uint32_t kSyncWord = 0xF531FAB6u;

constexpr uint8_t kFrameBytes = 26;
constexpr uint8_t kDataBytes = 24;

// INFO: fc 09mar26 2 is a position, 0 the pre-2024 frame, 3 a text, all readable unencrypted
constexpr uint8_t kMessageTypePosition = 2;

// INFO: fc 09mar26 the CRC is seeded with the CRC of the three sync bytes, which are not data
constexpr uint16_t kCrcInit = 0x051E;

constexpr uint8_t kAddressTypeIcao = 1;
constexpr uint8_t kAddressTypeFlarm = 2;

// INFO: fc 26sep26 SoftRF latest_encode sends 1 on ground, 2 airborne, 3 circling, and never 0
constexpr uint8_t kAirborneOnGround = 1;
constexpr uint8_t kAirborneFlying = 2;
constexpr uint8_t kAirborneCircling = 3;

uint16_t crc(const uint8_t* data);
void set_crc(uint8_t* frame);
bool crc_ok(const uint8_t* frame);

int correct(uint8_t* frame, const uint8_t* erasures, int max_erasures = 6);

// INFO: fc 19sep26 GPS-UTC is 18 s, the widest a receiver holding a fix can be wrong by
constexpr int32_t kAlptasKeyWindowS = 18;

wire::Status keyed_second(const uint8_t* frame, uint32_t rx_utc_s, uint32_t& keyed_utc_s);

// INFO: fc 09mar26 the address word is sent in clear, readable before anything is decrypted
uint32_t address(const uint8_t* frame);

struct Position {
    uint32_t address{0};
    uint8_t address_type{0};
    uint8_t aircraft_type{0};
    uint8_t airborne{0};
    int32_t latitude_1e7{0};
    int32_t longitude_1e7{0};
    int32_t altitude_m{0};
    int32_t speed_e1{0};
    int32_t climb_e1{0};
    uint16_t course_deg{0};
};

wire::Status decode(const uint8_t* frame, uint32_t rx_utc_s, int32_t reference_latitude_1e7,
                    int32_t reference_longitude_1e7, Position& out);

void encode(uint8_t* frame, const Position& position, uint32_t utc_s);

}

#endif
