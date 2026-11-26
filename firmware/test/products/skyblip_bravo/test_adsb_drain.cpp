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

// What bravo-d582 met outdoors: a flood of noise frames past the preamble detector.
TEST_CASE("bravo adsb: noise in impossible formats raises the threshold the LR2021 holds") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(1);
    const uint32_t until_ms = rig.now_ms + bravo::DetectionThreshold::kFalseAlarmWindowMs + 1000;
    while (rig.now_ms < until_ms) {
        for (int i = 0; i < 20; i++) {
            events::AdsbFrame noise{};
            noise.bytes.fill(static_cast<uint8_t>(i));
            noise.bytes[0] = static_cast<uint8_t>((6 + i % 4) << 3);
            (void)rig.product.adsb_frames().push(noise);
        }
        rig.run_for(10);
    }
    const int16_t raised = rig.product.adsb().detection_threshold().threshold_dbm();
    CHECK(raised > bravo::DetectionThreshold::kFloorDbm);
    rig.run_for(1500);
    CHECK(rig.product.board().receiver().statistics().probe.threshold_dbm == raised);
}
