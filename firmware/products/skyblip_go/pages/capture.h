#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_CAPTURE_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_CAPTURE_H

#include <cstdint>

#include "core/bus/state.h"
#include "core/diag/profile.h"
#include "products/skyblip_go/glass.h"

namespace skyblip::go {

constexpr diag::Profile kFirstCapture = diag::Profile::Full;

constexpr diag::Profile next_capture(diag::Profile profile) {
    return profile == diag::Profile::Full ? diag::Profile::PowerRun : kFirstCapture;
}

const char* capture_name(diag::Profile profile);

struct CaptureSnapshot {
    uint32_t uptime_s{0};
    bus::CaptureState capture{};
    uint32_t focus_keeps_s{0};
    diag::Profile focus{kFirstCapture};
    diag::Profile running{kFirstCapture};
    bool arming{false};
};

void draw_capture(ui::Canvas& fb, const CaptureSnapshot& snap);

}  // namespace skyblip::go

#endif
