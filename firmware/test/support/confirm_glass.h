// Harness, not a test: the seconds a prompt says it has left, read back off the glass, 0 for none.
#ifndef SKYBLIP_TEST_SUPPORT_CONFIRM_GLASS_H
#define SKYBLIP_TEST_SUPPORT_CONFIRM_GLASS_H

#include <cstdint>
#include <string>

#include "core/comms/config.h"
#include "products/skyblip_go/pages/confirm.h"
#include "test/support/glass_text.h"

namespace skyblip {

inline uint32_t seconds_left_on(const ui::Canvas& canvas) {
    for (uint32_t seconds = 1; seconds <= comms::kPromptCeilingMs / 1000u; seconds++) {
        const std::string text = "IT CANCELS IN " + std::to_string(seconds) + " S";
        if (reads_at(canvas, go::kConfirmLeftX, go::kConfirmExpiryY, text.c_str())) return seconds;
    }
    return 0;
}

}  // namespace skyblip

#endif
