// core/protocol/alptas.h: ALP-TAS air-frame codec, 2024 protocol (message type 2).
#ifndef SKYBLIP_CORE_PROTOCOL_ALPTAS_H
#define SKYBLIP_CORE_PROTOCOL_ALPTAS_H

#include <cstdint>

#include "core/model/aircraft.h"
#include "core/util/result.h"

namespace skyblip::protocol {

// 24 data bytes + 2 CRC bytes, as delivered by the receiver after Manchester
// decoding.
constexpr uint8_t kAlptasFrameBytes = 26;
constexpr uint8_t kAlptasDataBytes = 24;

// INFO: fc 09mar26 message type 2 is the position frame; 0 is the pre-2024
// frame and 3 is a text message. The type sits in the plaintext first word, so
// it can be read before the decrypt stages.
constexpr uint8_t kAlptasMsgTypePosition = 2;

// INFO: fc 09mar26 the frame CRC-16/CCITT is seeded with the CRC of the three
// sync bytes, which are not transmitted as data.
constexpr uint16_t kAlptasCrcInit = 0x051E;

uint16_t alptas_crc(const uint8_t* data);  // over kAlptasDataBytes
void alptas_set_crc(uint8_t* frame);
bool alptas_crc_ok(const uint8_t* frame);

int alptas_correct(uint8_t* frame, const uint8_t* err, int max_bad_bits = 6);

// INFO: fc 19sep26 GPS-UTC is 18 s, the widest a receiver holding a fix can be wrong by
constexpr int32_t kAlptasKeyWindowS = 18;

Status alptas_keyed_second(const uint8_t* frame, uint32_t rx_utc, uint32_t& keyed_utc);

// INFO: fc 09mar26 the address word is sent in clear: a receiver can read the
// sender's identity before, and without, decrypting anything.
uint32_t alptas_address(const uint8_t* frame);

uint8_t alptas_type_to_adsl_cat(uint8_t alptas_type);  // inverse of adsl_cat_to_alptas
// INFO: fc 26sep26 SoftRF latest_encode sends 1 on ground, 2 airborne, 3 circling, and never 0
uint8_t alptas_flight_state(uint8_t alptas_airborne);
uint8_t alptas_addr_type_to_table(uint8_t alptas_addr_type);
uint8_t adsl_table_to_alptas_addr_type(uint8_t addr_table);

// Decrypt + unpack. Position is coded relative to the receiver, so it needs own
// position, and the key stage needs the UTC second the frame arrived in.
Status alptas_decode(const uint8_t* frame, uint32_t rx_utc, int32_t ref_lat_1e7,
                     int32_t ref_lon_1e7, model::AircraftObs& out);

// Pack + encrypt the same fields back, so the simulator can fly ALP-TAS traffic
// and the round trip is a test.
Status alptas_encode(uint8_t* frame, const model::AircraftObs& obs, uint32_t utc,
                     int32_t ref_lat_1e7, int32_t ref_lon_1e7);

}

#endif
