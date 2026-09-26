// Harness, not a test: a simulated device run past its first fixes, stepped, its air tape counted.
#ifndef SKYBLIP_TEST_SUPPORT_SIMULATOR_RUN_H
#define SKYBLIP_TEST_SUPPORT_SIMULATOR_RUN_H

#include <cstdint>

#include "core/gnss/first_fix.h"
#include "simulator/simulator.h"

namespace skyblip {

// F5: own-ship holds every burst until the receiver's first solutions have
// settled, so a transmit test that starts the clock at zero is a test of the
// settling window and nothing else. This is the far side of it, with the tape
// wiped so the bursts that follow are the ones under test.
inline uint32_t past_settling(simulator::Simulator& h) {
    h.run(gnss::kFirstFixSettleMs);
    h.world().air().clear();
    return gnss::kFirstFixSettleMs;
}

inline void run_on(simulator::Simulator& h, uint32_t from_ms, uint32_t for_ms,
                   uint32_t step_ms = simulator::Simulator::kStepMs) {
    for (uint32_t t = from_ms; t <= from_ms + for_ms; t += step_ms) h.step(t);
}

inline int count_of(const simulator::Air& air, simulator::AirEvent want) {
    int n = 0;
    for (int i = 0; i < air.record_count(); i++)
        if (air.record(i).event == want) n++;
    return n;
}

}  // namespace skyblip

#endif
