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
    for (uint32_t now_ms = from_ms; now_ms <= to_ms; now_ms += 10) {
        chip.tick(now_ms);
        driver.service(now_ms);
        driver.poll();
    }
}

// How long bring-up takes before the first configuration sentence is on the
// wire: the wake byte, the half second the receiver spends waking, and the
// identification handshake. Written out because every timed case below has to
// clear it, and a magic 1500 in six places is how a test stops being a
// specification.
constexpr uint32_t kBringUpLeadMs = parts::L76k::kWakeDelayMs + parts::L76k::kIdentifyWindowMs;

// How long until the receiver solves four times a second: the four sentences
// and their verification at 9600, the second the raised port waits for its
// first sentence, then the two that ask for 4 Hz and the window that counts it.
constexpr uint32_t kConfiguredMs =
    kBringUpLeadMs + (parts::L76k::kCommandCount - 1) * parts::L76k::kCommandGapMs +
    parts::L76k::kVerifyWindowMs + parts::L76k::kFactorySolutionPeriodMs +
    (parts::L76k::kQuarterRateCommandCount - 1) * parts::L76k::kCommandGapMs +
    parts::L76k::kQuarterVerifyWindowMs + 100;

}  // namespace skyblip

#endif
