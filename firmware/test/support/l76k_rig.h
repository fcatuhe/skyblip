// Harness, not a test: the L76K driver ticked against its model, as the board polls it.
#ifndef SKYBLIP_TEST_SUPPORT_L76K_RIG_H
#define SKYBLIP_TEST_SUPPORT_L76K_RIG_H

#include <cstdint>

#include "hardware/parts/l76k/l76k.h"
#include "hardware/parts/l76k/model.h"

namespace skyblip {

// One service call and one drain per 10 ms runtime tick, which is how the board
// polls this part.
inline void run(parts::L76k& driver, models::L76k& chip, uint32_t from_ms, uint32_t to_ms) {
    for (uint32_t t = from_ms; t <= to_ms; t += 10) {
        chip.tick(t);
        driver.service(t);
        driver.poll();
    }
}

// How long bring-up takes before the first configuration sentence is on the
// wire: the wake byte, the half second the receiver spends waking, and the
// identification handshake. Written out because every timed case below has to
// clear it, and a magic 1500 in six places is how a test stops being a
// specification.
constexpr uint32_t kBringUpLeadMs = parts::L76k::kWakeDelayMs + parts::L76k::kIdentifyWindowMs;

}  // namespace skyblip

#endif
