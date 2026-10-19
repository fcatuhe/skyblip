#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_CAPTURE_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_CAPTURE_H

#include <cstdint>

#include "core/bus/state.h"
#include "core/diagnostics/profile.h"
#include "products/skyblip_go/glass.h"

namespace skyblip::go {

constexpr diagnostics::Profile kFirstCapture = diagnostics::Profile::Full;

constexpr diagnostics::Profile next_capture(diagnostics::Profile profile) {
    switch (profile) {
        case diagnostics::Profile::Full: return diagnostics::Profile::PowerRun;
        case diagnostics::Profile::PowerRun: return diagnostics::Profile::FlightRun;
        case diagnostics::Profile::FlightRun: break;
    }
    return kFirstCapture;
}

const char* capture_name(diagnostics::Profile profile);

struct CaptureSnapshot {
    uint32_t uptime_s{0};
    bus::CaptureState capture{};
    uint32_t focus_keeps_s{0};
    diagnostics::Profile focus{kFirstCapture};
    diagnostics::Profile running{kFirstCapture};
    bool arming{false};
};

void draw_capture(ui::Canvas& canvas, const CaptureSnapshot& snapshot);

}  // namespace skyblip::go

#endif
