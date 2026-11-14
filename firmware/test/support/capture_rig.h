// Harness, not a test: a product rig with its capture armed over the link, and bench records fed.
#ifndef SKYBLIP_TEST_SUPPORT_CAPTURE_RIG_H
#define SKYBLIP_TEST_SUPPORT_CAPTURE_RIG_H

#include <cstdint>
#include <string>

#include "core/diagnostics/payload.h"
#include "doctest/doctest.h"
#include "test/support/product_rig.h"
#include "test/support/rig_moves.h"

namespace skyblip {

// A page's ask and the two presses on the glass that allow it, as a pilot orders one in the field.
inline void order_over_the_link(Rig& rig, uint32_t& now_ms, const char* profile) {
    const std::string json =
        std::string("{\"command\":\"capture\",\"profile\":\"") + profile + "\"}";
    rig.send(json.c_str());
    rig.run(now_ms, now_ms + 3000);
    now_ms += 3000;
    REQUIRE(rig.product.config_link().config().pending() == comms::Pending::Capture);
    rig.double_press(now_ms);
    rig.run(now_ms, now_ms + 1000);
    now_ms += 1000;
}

inline void arm_over_the_link(Rig& rig, uint32_t& now_ms, const char* profile = "full") {
    order_over_the_link(rig, now_ms, profile);
    REQUIRE(rig.product.diagnostics().armed());
}

inline void stop_over_the_link(Rig& rig, uint32_t& now_ms) {
    order_over_the_link(rig, now_ms, "stop");
    REQUIRE_FALSE(rig.product.diagnostics().armed());
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
