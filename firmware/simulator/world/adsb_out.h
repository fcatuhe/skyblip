// Harness, not a test: the world's ADS-B Out on air, preamble and sync ahead of the stack's
// squitter frames.
#ifndef SKYBLIP_SIMULATOR_WORLD_ADSB_OUT_H
#define SKYBLIP_SIMULATOR_WORLD_ADSB_OUT_H

#include <cstdint>

#include "protocols/adsb/message.h"
#include "protocols/adsb/squitter.h"

namespace skyblip::simulator {

// INFO: fc 11nov26 the 8 us preamble at two chips a microsecond: pulses at 0, 1, 3.5 and 4.5 us
constexpr uint8_t kAdsbPreambleAir[2] = {0xA1, 0x40};

struct AdsbAir {
    uint8_t chips[sizeof(kAdsbPreambleAir) + 1 + adsb::kChipBytes + 1]{};
};

inline AdsbAir adsb_air(adsb::Squitter frame) {
    adsb::seal(frame.bytes);
    AdsbAir air;
    air.chips[0] = kAdsbPreambleAir[0];
    air.chips[1] = kAdsbPreambleAir[1];
    air.chips[2] = adsb::kSyncChips;
    adsb::chips_of(frame.bytes, air.chips + 3);
    return air;
}

}  // namespace skyblip::simulator

#endif
