#ifndef SKYBLIP_CORE_COMMS_PEER_H
#define SKYBLIP_CORE_COMMS_PEER_H

#include <cstdint>

#include "core/comms/link_registry.h"
#include "core/util/result.h"
#include "core/util/span.h"

namespace skyblip::comms {

// INFO: fc 11nov26 the observation at its widest, the widest peer frame (#177)
constexpr uint16_t kPeerPayloadFloorBytes = 126;

constexpr int kPeerClaimWidestBytes = 96;

// INFO: fc 11nov26 schemas/link.v1.json: protocol_version 1 is the three peer frames as written
constexpr uint32_t kPeerProtocolVersion = 1;

struct PeerClaim {
    uint32_t boot_epoch{0};
    bool utc_valid{false};
    uint32_t at_s{0};
    bool phase_valid{false};
    uint16_t into_ms{0};
    bool fix_valid{false};
    int32_t latitude_1e7{0};
    int32_t longitude_1e7{0};
    int32_t altitude_hae_mm{0};
    uint32_t fix_age_ms{0};
    bool pressure_altitude_valid{false};
    int32_t pressure_altitude_mm{0};
    uint32_t pressure_altitude_age_ms{0};
    uint32_t own_icao{0};
    uint32_t alarm_passes{0};
    uint8_t alarm_volume{0};
};

// INFO: fc 11nov26 0 when the frame would not fit capacity, never a frame with a key missing
int write_peer_claim(const PeerClaim& claim, uint8_t* out, int capacity);

struct PeerHealth {
    uint32_t boot_epoch{0};
    bool own_icao_set{false};
};

Result<link::Command> peer_command(ConstByteSpan frame);
// INFO: fc 11nov26 Invalid without protocol_version, Unsupported for one this build does not speak
Result<PeerHealth> read_health(ConstByteSpan frame);

}  // namespace skyblip::comms

#endif
