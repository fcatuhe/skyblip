// Harness, not a test: the DF17 frames a transmitter sends, heard through the real decoder.
#ifndef SKYBLIP_TEST_SUPPORT_ADSB_FRAMES_H
#define SKYBLIP_TEST_SUPPORT_ADSB_FRAMES_H

#include <cstdint>

#include "core/protocol/adsb.h"
#include "core/util/result.h"
#include "simulator/world/adsb_out.h"
#include "test/support/adsb_chips.h"

namespace skyblip {

using simulator::adsb_air_velocity;
using simulator::adsb_airborne_position;
using simulator::adsb_altitude_field;
using simulator::adsb_cpr_encode;
using simulator::adsb_frame;
using simulator::adsb_ground_velocity;
using simulator::adsb_identification;
using simulator::adsb_longitude_zones;
using simulator::adsb_operational_status;
using simulator::adsb_set_cpr;
using simulator::adsb_set_message_bits;
using simulator::adsb_signed_field;
using simulator::adsb_surface_position;
using simulator::AdsbFrame;

inline Status adsb_hear(AdsbFrame frame, protocol::AdsbMessage& message) {
    adsb_seal(frame.bytes);
    uint8_t chips[protocol::kAdsbChipBytes];
    adsb_chips(frame.bytes, chips);
    return protocol::adsb_receive(chips, sizeof(chips), protocol::AdsbReception{0, -70}, message);
}

}

#endif
