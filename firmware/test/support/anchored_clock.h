// Harness, not a test: a clock with UTC and a locked PPS, the only kind a slot transmits on.
#ifndef SKYBLIP_TEST_SUPPORT_ANCHORED_CLOCK_H
#define SKYBLIP_TEST_SUPPORT_ANCHORED_CLOCK_H

#include "core/timing/slot.h"

namespace skyblip::timing {

inline ClockState anchored() { return ClockState{true, true, 0}; }

}  // namespace skyblip::timing

#endif
