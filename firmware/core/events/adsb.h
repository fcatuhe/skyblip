#ifndef SKYBLIP_CORE_EVENTS_ADSB_H
#define SKYBLIP_CORE_EVENTS_ADSB_H

#include <array>
#include <cstdint>

#include "core/protocol/adsb.h"

namespace skyblip::events {

struct AdsbFrame {
    uint64_t at_us;
    int8_t rssi_dbm;
    bool rssi_valid;
    std::array<uint8_t, protocol::kAdsbChipBytes> chips;
};

// INFO: fc 11nov26 DO-260B Table 2-105 class A0, the figure protocol::AdsbAssembly::kMaxAircraft is
constexpr uint32_t kAdsbDesignAircraft = 100;
// INFO: fc 11nov26 DO-260B 2.2.3.3.1.3: 6.2 squitters a second averaged over 60 s, so an average
// load, no burst bound
constexpr uint32_t kAdsbSquittersPerAircraftE1 = 62;
// TODO: fc 11nov26 a drain window chosen and not yet measured, until the #169 bench reads the drops
constexpr uint32_t kAdsbQueueDrainWindowMs = 100;
constexpr int kAdsbQueueFrames = static_cast<int>(
    kAdsbDesignAircraft * kAdsbSquittersPerAircraftE1 * kAdsbQueueDrainWindowMs / 10000);

}  // namespace skyblip::events

#endif
