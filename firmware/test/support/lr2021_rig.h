// Harness, not a test: the LR2021 driver wired to its model, and a DF17 frame as the air carries
// it.
#ifndef SKYBLIP_TEST_SUPPORT_LR2021_RIG_H
#define SKYBLIP_TEST_SUPPORT_LR2021_RIG_H

#include <cstdint>

#include "core/protocol/adsb.h"
#include "hardware/parts/lr2021/lr2021.h"
#include "hardware/parts/lr2021/model.h"
#include "test/support/adsb_chips.h"
#include "test/support/adsb_frames.h"

namespace skyblip {

inline parts::Lr2021 lr2021_for(models::Lr2021& chip) {
    return parts::Lr2021(chip, chip, chip, chip.busy_pin, chip.reset_pin, chip.irq_pin,
                         chip.irq_dio);
}

// INFO: fc 11nov26 the 8 us preamble at two chips a microsecond: pulses at 0, 1, 3.5 and 4.5 us
constexpr uint8_t kAdsbPreambleAir[2] = {0xA1, 0x40};

struct AdsbAir {
    uint8_t chips[sizeof(kAdsbPreambleAir) + 1 + protocol::kAdsbChipBytes + 1]{};
};

inline AdsbAir adsb_air(AdsbFrame frame) {
    adsb_seal(frame.bytes);
    AdsbAir air;
    air.chips[0] = kAdsbPreambleAir[0];
    air.chips[1] = kAdsbPreambleAir[1];
    air.chips[2] = protocol::kAdsbSyncChips;
    adsb_chips(frame.bytes, air.chips + 3);
    return air;
}

inline parts::Lr2021 lr2021_receiving(models::Lr2021& chip) {
    parts::Lr2021 radio = lr2021_for(chip);
    (void)radio.begin(parts::lr2021::kDetectionThresholdDbm);
    (void)radio.start_receive();
    return radio;
}

}  // namespace skyblip

#endif
