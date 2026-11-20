// Harness, not a test: a receiver at the rate the Go asks, for a rig with no L76K behind it.
#ifndef SKYBLIP_TEST_SUPPORT_RECEIVER_AT_RATE_H
#define SKYBLIP_TEST_SUPPORT_RECEIVER_AT_RATE_H

#include "ports/gnss.h"

namespace skyblip {

struct ReceiverAtRate : ports::Gnss {
    ports::GnssHealth health() const override {
        ports::GnssHealth held{};
        held.baud = 115200;
        held.config_stage = ports::GnssConfigStage::Ready;
        held.identified = true;
        held.rate_held = rate_held;
        return held;
    }
    bool rate_held{true};
};

}  // namespace skyblip

#endif
