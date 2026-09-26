// Harness, not a test: the rig's config service, the rig on the ground, a receiver with no fix.
#ifndef SKYBLIP_TEST_SUPPORT_UPDATE_RIG_H
#define SKYBLIP_TEST_SUPPORT_UPDATE_RIG_H

#include <cstdint>

#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

inline void on_ground(Rig& rig, uint32_t& t) {
    rig.push_fix(/*alt_m=*/0, /*updates=*/1);
    rig.run(t, t + 200);
    t += 200;
}

inline void receiver_speaks_without_a_fix(Rig& rig) {
    gnss::GnssSolution f{};
    f.fix_valid = false;
    f.updates = 1;
    rig.product.bus().gnss.push(f);
}

inline comms::ConfigService& config(Rig& rig) { return rig.product.config().config(); }

}  // namespace

#endif
