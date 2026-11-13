// Harness, not a test: the world's ADS-B Out on air, preamble and sync ahead of the stack's
// squitter frames.
#ifndef SKYBLIP_SIMULATOR_WORLD_ADSB_OUT_H
#define SKYBLIP_SIMULATOR_WORLD_ADSB_OUT_H

#include <cstdint>

#include "protocols/adsb/message.h"
#include "protocols/adsb/test/squitter.h"

namespace skyblip::simulator {

using adsb::adsb_air_velocity;
using adsb::adsb_airborne_position;
using adsb::adsb_altitude_field;
using adsb::adsb_chips;
using adsb::adsb_cpr_encode;
using adsb::adsb_frame;
using adsb::adsb_ground_velocity;
using adsb::adsb_identification;
using adsb::adsb_longitude_zones;
using adsb::adsb_movement_code;
using adsb::adsb_operational_status;
using adsb::adsb_seal;
using adsb::adsb_set_cpr;
using adsb::adsb_set_message_bits;
using adsb::adsb_signed_field;
using adsb::adsb_surface_position;
using adsb::AdsbFrame;
using adsb::kAdsbParityOffsetBytes;

// INFO: fc 11nov26 the 8 us preamble at two chips a microsecond: pulses at 0, 1, 3.5 and 4.5 us
constexpr uint8_t kAdsbPreambleAir[2] = {0xA1, 0x40};

struct AdsbAir {
    uint8_t chips[sizeof(kAdsbPreambleAir) + 1 + adsb::kChipBytes + 1]{};
};

inline AdsbAir adsb_air(AdsbFrame frame) {
    adsb_seal(frame.bytes);
    AdsbAir air;
    air.chips[0] = kAdsbPreambleAir[0];
    air.chips[1] = kAdsbPreambleAir[1];
    air.chips[2] = adsb::kSyncChips;
    adsb_chips(frame.bytes, air.chips + 3);
    return air;
}

}  // namespace skyblip::simulator

#endif
