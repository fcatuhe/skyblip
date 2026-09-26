#include "core/power/wake.h"

namespace skyblip::power {

const char* to_string(BootPath path) {
    switch (path) {
        case BootPath::SleepAgain: return "SLEEP AGAIN";
        case BootPath::Run: break;
    }
    return "RUN";
}

const char* to_string(RefusedFrame frame) {
    switch (frame) {
        case RefusedFrame::Wordmark: return "WORDMARK";
        case RefusedFrame::FlatCell: return "FLAT CELL";
        case RefusedFrame::Leave: break;
    }
    return "LEAVE";
}

namespace {

bool below(const BootCell& cell, uint16_t floor_mv) {
    if (!cell.valid || cell.external_power) return false;
    if (cell.millivolts <= kImplausibleFloorMv) return false;
    return cell.millivolts < floor_mv;
}

bool too_flat_to_run(const BootCell& cell) { return below(cell, kBootLockoutMv); }

// INFO: fc 23sep26 a unit that reset itself was running a moment ago: only the cutoff may stop it
bool restarted(ResetCause causes) {
    return has_cause(causes, ResetCause::Watchdog) || has_cause(causes, ResetCause::Lockup) ||
           has_cause(causes, ResetCause::Software);
}

}  // namespace

ButtonWake button_wake_after_refusal(const BootCell& cell) {
    return too_flat_to_run(cell) ? ButtonWake::Withheld : ButtonWake::Armed;
}

RefusedFrame refused_frame(const BootCell& cell, bool flat_on_glass) {
    if (too_flat_to_run(cell)) return flat_on_glass ? RefusedFrame::Leave : RefusedFrame::FlatCell;
    return flat_on_glass ? RefusedFrame::Wordmark : RefusedFrame::Leave;
}

BootPath boot_path(ResetCause causes, bool button_down, const BootCell& cell) {
    if (restarted(causes) ? below(cell, kCutoffMv) : too_flat_to_run(cell))
        return BootPath::SleepAgain;
    if (button_down) return BootPath::Run;
    if (has_cause(causes, ResetCause::Pin)) return BootPath::Run;
    // Both, not either. VBUS alone is not enough to refuse a boot: the bit is
    // only ever set on a wake from SYSTEM OFF, and if a future silicon or a
    // future adapter reported it any other way, refusing on it alone would be a
    // device that will not switch on.
    if (!has_cause(causes, ResetCause::LowPowerWake)) return BootPath::Run;
    if (!has_cause(causes, ResetCause::UsbVbus)) return BootPath::Run;
    return BootPath::SleepAgain;
}

}  // namespace skyblip::power
