#ifndef SKYBLIP_CORE_EVENTS_ADSB_H
#define SKYBLIP_CORE_EVENTS_ADSB_H

#include <array>
#include <cstdint>

#include "protocols/adsb/message.h"

namespace skyblip::events {

struct AdsbFrame {
    uint64_t at_us;
    int8_t rssi_dbm;
    bool rssi_valid;
    std::array<uint8_t, adsb::kFrameBytes> bytes;
};

// INFO: fc 11nov26 DO-260B Table 2-105 class A0, the figure adsb::Assembly::kMaxAircraft is
constexpr uint32_t kAdsbDesignAircraft = 100;
// INFO: fc 11nov26 DO-260B 2.2.3.3.1.3: 6.2 squitters a second averaged over 60 s, so an average
// load, no burst bound
constexpr uint32_t kAdsbSquittersPerAircraftE1 = 62;
// TODO: fc 11nov26 a drain window chosen and not yet measured, until the #169 bench reads the drops
constexpr uint32_t kAdsbQueueDrainWindowMs = 100;
constexpr int kAdsbLoadFrames = static_cast<int>(kAdsbDesignAircraft * kAdsbSquittersPerAircraftE1 *
                                                 kAdsbQueueDrainWindowMs / 10000);
// INFO: fc 21nov26 bus::Queue masks its index, so the queue takes the power of two above the load
constexpr int kAdsbQueueFrames = 64;
static_assert(kAdsbQueueFrames >= kAdsbLoadFrames && kAdsbQueueFrames < 2 * kAdsbLoadFrames);

}  // namespace skyblip::events

#endif
