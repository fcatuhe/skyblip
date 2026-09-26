// Harness, not a test: the product rig taxiing, or in flight, for a number of seconds.
#ifndef SKYBLIP_TEST_SUPPORT_RIG_MOVES_H
#define SKYBLIP_TEST_SUPPORT_RIG_MOVES_H

#include <cstdint>

#include "test/support/product_rig.h"

namespace skyblip {

// A takeoff, some minutes of flight, and a landing, in the units core/flight
// judges. 50 m/s is 200 quarter-metres per second; standing still is zero.
inline void taxi(Rig& rig, uint32_t& t, uint32_t seconds) { rig.seconds(t, seconds, 0, 300); }
inline void fly(Rig& rig, uint32_t& t, uint32_t seconds) { rig.seconds(t, seconds, 50000, 800); }

}  // namespace skyblip

#endif
