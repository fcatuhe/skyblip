// A 1090 MHz flood costs frames, never the loop: each pass folds a drain window's design load.
#include <numeric>

#include "doctest/doctest.h"
#include "test/support/bravo_rig.h"

using namespace skyblip;

namespace {

uint32_t decoded(Bravo& product) {
    const auto& formats = product.adsb().formats();
    return std::accumulate(formats.begin(), formats.end(), 0u);
}

void fill(bus::AdsbQueue& queue, int frames) {
    for (int i = 0; i < frames; i++) {
        events::AdsbFrame noise{};
        noise.bytes.fill(static_cast<uint8_t>(0x5A + i));
        REQUIRE(queue.push(noise));
    }
}

}  // namespace

TEST_CASE("bravo adsb: a pass decodes at most a drain window's design load, the rest next pass") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(1);
    const uint32_t before = decoded(rig.product);
    fill(rig.product.adsb_frames(), events::kAdsbQueueFrames);

    rig.product.adsb().tick(rig.now_ms);
    CHECK(decoded(rig.product) - before == bravo::AdsbService::kFramesPerPassCeiling);
    CHECK(rig.product.adsb().due_in_ms(rig.now_ms) == 0);

    rig.product.adsb().tick(rig.now_ms);
    CHECK(decoded(rig.product) - before == events::kAdsbQueueFrames);
    CHECK(rig.product.adsb().due_in_ms(rig.now_ms) > 0);
}
