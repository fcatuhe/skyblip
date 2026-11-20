// Harness, not a test: the fix lag a schedule case hands Transmitter::attempt.
#ifndef SKYBLIP_TEST_SUPPORT_FRESH_FIX_H
#define SKYBLIP_TEST_SUPPORT_FRESH_FIX_H

#include <cstdint>

namespace skyblip {

// Solved at the end of the second, so fresh at any instant in it: the case reads the schedule.
constexpr int32_t kFixSolvedAtSecondEnd = -1000;

}  // namespace skyblip

#endif
