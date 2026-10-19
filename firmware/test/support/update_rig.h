// Harness, not a test: the rig's config service, the rig on the ground, a receiver with no fix.
#ifndef SKYBLIP_TEST_SUPPORT_UPDATE_RIG_H
#define SKYBLIP_TEST_SUPPORT_UPDATE_RIG_H

#include <cstdint>

#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

inline void on_ground(Rig& rig, uint32_t& now_ms) {
    rig.push_fix(/*altitude_m=*/0, /*updates=*/1);
    rig.run(now_ms, now_ms + 200);
    now_ms += 200;
}

inline void receiver_speaks_without_a_fix(Rig& rig) {
    gnss::GnssSolution solution{};
    solution.fix_valid = false;
    solution.sentences = 1;
    rig.product.bus().gnss.push(solution);
}

inline comms::ConfigService& config(Rig& rig) { return rig.product.config_link().config(); }

}  // namespace

#endif
