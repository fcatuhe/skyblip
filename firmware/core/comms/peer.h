#ifndef SKYBLIP_CORE_COMMS_PEER_H
#define SKYBLIP_CORE_COMMS_PEER_H

#include <cstdint>

#include "core/comms/link_registry.h"
#include "core/events/stamp.h"
#include "core/model/aircraft.h"
#include "core/util/result.h"
#include "core/util/span.h"

namespace skyblip::comms {

// INFO: fc 11nov26 the observation at its widest, the widest peer frame (#177)
constexpr uint16_t kPeerPayloadFloorBytes = 126;

constexpr int kPeerClaimWidestBytes = 98;

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
    uint32_t fix_age_ms{0};
    bool altitude_hae_valid{false};
    int32_t altitude_hae_mm{0};
    uint32_t altitude_hae_age_ms{0};
    bool pressure_altitude_valid{false};
    int32_t pressure_altitude_mm{0};
    uint32_t pressure_altitude_age_ms{0};
    uint32_t own_icao{0};
    uint32_t alarm_passes{0};
    uint8_t alarm_volume{0};
    bool paired{false};
};

// INFO: fc 11nov26 0 when the frame would not fit capacity, never a frame with a key missing
int write_peer_claim(const PeerClaim& claim, uint8_t* out, int capacity);

// INFO: fc 11nov26 fix_valid and fix_age_ms are the position's, the height has its own pair
Result<PeerClaim> read_peer_claim(ConstByteSpan frame);

enum class ExportReference : uint8_t { None = 0, Go = 1, Own = 2 };

struct PeerHealth {
    uint32_t boot_epoch{0};
    bool listening{false};
    bool utc_valid{false};
    bool own_icao_set{false};
    bool last_sequence_valid{false};
    uint32_t last_sequence{0};
    ExportReference export_reference{ExportReference::None};
    bool paired{false};
};

int write_health(const PeerHealth& health, uint8_t* out, int capacity);

struct ObservationFrame {
    uint32_t boot_epoch{0};
    uint32_t sequence{0};
    const char* callsign{nullptr};
    bool callsign_heard_valid{false};
    uint32_t callsign_heard_s{0};
};

int write_observation(const model::AircraftObservation& observation, const ObservationFrame& frame,
                      uint8_t* out, int capacity);

// INFO: fc 12nov26 schemas/link.v1.json, observation: a callsign of at most eight characters
constexpr int kPeerCallsignBytes = 9;

struct PeerObservation {
    uint32_t boot_epoch{0};
    uint32_t sequence{0};
    events::Stamp at{};
    model::AircraftObservation observation{};
    char callsign[kPeerCallsignBytes]{};
    uint32_t callsign_heard_s{0};
    bool callsign_dated{false};
};

// INFO: fc 12nov26 what a paired Go makes of its Bravo's health, the status frame's bravo
enum class BravoState : uint8_t {
    Unpaired = 0,
    Waiting = 1,
    Live = 2,
    LinkDown = 3,
    ReceiverDown = 4,
    NoUtc = 5
};

constexpr bool lost(BravoState state) {
    return state == BravoState::LinkDown || state == BravoState::ReceiverDown ||
           state == BravoState::NoUtc;
}

Result<link::Command> peer_command(ConstByteSpan frame);
// INFO: fc 11nov26 Invalid without protocol_version, Unsupported for one this build does not speak
Result<PeerHealth> read_health(ConstByteSpan frame);
// INFO: fc 12nov26 at_ms and received are the reader's, on its own clock, so both are left at 0
Result<PeerObservation> read_observation(ConstByteSpan frame);

}  // namespace skyblip::comms

#endif
