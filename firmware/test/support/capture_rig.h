// Harness, not a test: a product rig with its capture armed from the page, and bench records fed.
#ifndef SKYBLIP_TEST_SUPPORT_CAPTURE_RIG_H
#define SKYBLIP_TEST_SUPPORT_CAPTURE_RIG_H

#include <cstdint>

#include "core/diagnostics/payload.h"
#include "doctest/doctest.h"
#include "test/support/product_rig.h"
#include "test/support/rig_moves.h"

namespace skyblip {

inline void open_capture_page(Rig& rig, uint32_t& now_ms) {
    rig.show(now_ms, go::Page::Capture);
    rig.run(now_ms, now_ms + 2000);
    now_ms += 2000;
    REQUIRE(rig.product.screen().page() == go::Page::Capture);
}

inline void arm_from_the_page(Rig& rig, uint32_t& now_ms) {
    open_capture_page(rig, now_ms);
    rig.double_press(now_ms);
    rig.run(now_ms, now_ms + 200);
    now_ms += 200;
}

inline diagnostics::Record bench_record(uint32_t at_s) {
    diagnostics::Instant at{};
    at.at_s = at_s;
    at.utc_dated = true;
    diagnostics::Gnss value{};
    value.satellites = 9;
    value.fix_valid = true;
    return diagnostics::record_of(value, at);
}

inline uint32_t feed(Rig& rig, uint32_t& now_ms, uint32_t records) {
    uint32_t pushed = 0;
    while (pushed < records && rig.product.capture().capturing()) {
        for (int i = 0; i < diagnostics::Recorder::kMaxRecords && pushed < records; i++)
            if (rig.product.diagnostics().record(bench_record(Rig::kBaseUtcS + pushed))) pushed++;
        rig.run(now_ms, now_ms + 50);
        now_ms += 50;
    }
    rig.run(now_ms, now_ms + 200);
    now_ms += 200;
    return pushed;
}

}  // namespace skyblip

#endif
