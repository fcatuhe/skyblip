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
inline uint32_t past_settling(simulator::Simulator& simulator) {
    simulator.run(gnss::kFirstFixSettleMs);
    simulator.world().air().clear();
    return gnss::kFirstFixSettleMs;
}

inline void run_on(simulator::Simulator& simulator, uint32_t from_ms, uint32_t for_ms,
                   uint32_t step_ms = simulator::Simulator::kStepMs) {
    for (uint32_t now_ms = from_ms; now_ms <= from_ms + for_ms; now_ms += step_ms)
        simulator.step(now_ms);
}

inline int count_of(const simulator::Air& air, simulator::AirEvent want) {
    int n = 0;
    for (int i = 0; i < air.record_count(); i++)
        if (air.record_at(i).event == want) n++;
    return n;
}

}  // namespace skyblip

#endif
