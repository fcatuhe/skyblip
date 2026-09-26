// Harness, not a test: the SSD1681 driver wired to its model, and a refresh driven to ready.
#ifndef SKYBLIP_TEST_SUPPORT_SSD1681_RIG_H
#define SKYBLIP_TEST_SUPPORT_SSD1681_RIG_H

#include "doctest/doctest.h"
#include "hardware/parts/ssd1681/model.h"
#include "hardware/parts/ssd1681/ssd1681.h"

namespace skyblip {

inline parts::Ssd1681 make(models::Ssd1681& f) {
    return parts::Ssd1681(f, f, f, f.dc, f.rst, f.busy);
}

// Drives the present to ready cycle to completion, as the screen service would
// across ticks.
inline void settle(parts::Ssd1681& d, uint32_t issued_ms) {
    CHECK_FALSE(d.ready(issued_ms));
    CHECK(d.ready(issued_ms + parts::Ssd1681::kReadyAfterFullMs));
}

}  // namespace skyblip

#endif
