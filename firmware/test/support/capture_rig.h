// Harness, not a test: a product rig taxied or flown, its capture armed from the page, and fed.
#ifndef SKYBLIP_TEST_SUPPORT_CAPTURE_RIG_H
#define SKYBLIP_TEST_SUPPORT_CAPTURE_RIG_H

#include <cstdint>

#include "core/diag/payload.h"
#include "doctest/doctest.h"
#include "test/support/product_rig.h"

namespace skyblip {

inline void taxi(Rig& rig, uint32_t& t, uint32_t seconds) { rig.seconds(t, seconds, 0, 300); }
inline void fly(Rig& rig, uint32_t& t, uint32_t seconds) { rig.seconds(t, seconds, 50000, 800); }

inline void open_capture_page(Rig& rig, uint32_t& t) {
    rig.show(t, go::Page::Capture);
    rig.run(t, t + 2000);
    t += 2000;
    REQUIRE(rig.product.screen().page() == go::Page::Capture);
}

inline void arm_from_the_page(Rig& rig, uint32_t& t) {
    open_capture_page(rig, t);
    rig.double_press(t);
    rig.run(t, t + 200);
    t += 200;
}

inline diag::Record bench_record(uint32_t at_s) {
    diag::Instant at{};
    at.at_s = at_s;
    at.utc_dated = true;
    diag::Gnss value{};
    value.sats = 9;
    value.fix_valid = true;
    return diag::record_of(value, at);
}

inline uint32_t feed(Rig& rig, uint32_t& t, uint32_t records) {
    uint32_t pushed = 0;
    while (pushed < records && rig.product.capture().capturing()) {
        for (int i = 0; i < diag::Recorder::kCapacity && pushed < records; i++)
            if (rig.product.diag().record(bench_record(Rig::kUtcBase + pushed))) pushed++;
        rig.run(t, t + 50);
        t += 50;
    }
    rig.run(t, t + 200);
    t += 200;
    return pushed;
}

}  // namespace skyblip

#endif
