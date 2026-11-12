// Harness, not a test: the LR2021 driver wired to its model, and a DF17 frame as the air carries
// it.
#ifndef SKYBLIP_TEST_SUPPORT_LR2021_RIG_H
#define SKYBLIP_TEST_SUPPORT_LR2021_RIG_H

#include <cstdint>

#include "core/protocol/adsb.h"
#include "hardware/parts/lr2021/lr2021.h"
#include "hardware/parts/lr2021/model.h"
#include "simulator/world/adsb_out.h"
#include "test/support/adsb_frames.h"

namespace skyblip {

inline parts::Lr2021 lr2021_for(models::Lr2021& chip) {
    return parts::Lr2021(chip, chip, chip, chip.busy_pin, chip.reset_pin, chip.irq_pin,
                         chip.irq_dio);
}

using simulator::adsb_air;
using simulator::AdsbAir;
using simulator::kAdsbPreambleAir;

inline parts::Lr2021 lr2021_receiving(models::Lr2021& chip) {
    parts::Lr2021 radio = lr2021_for(chip);
    (void)radio.begin(parts::lr2021::kDetectionThresholdDbm);
    (void)radio.start_receive();
    return radio;
}

}  // namespace skyblip

#endif
