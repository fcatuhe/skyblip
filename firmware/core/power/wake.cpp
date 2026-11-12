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
    if (cell.cell_mv <= kImplausibleFloorMv) return false;
    return cell.cell_mv < floor_mv;
}

bool too_flat_to_run(const BootCell& cell) { return below(cell, kBootLockoutMv); }

// INFO: fc 23sep26 a unit that reset itself was running a moment ago: only the cutoff may stop it
bool restarted(ResetCause causes) {
    return has_cause(causes, ResetCause::Watchdog) || has_cause(causes, ResetCause::Lockup) ||
           has_cause(causes, ResetCause::Software);
}

}  // namespace

RefusedFrame refused_frame(const BootCell& cell, CellOnGlass on_glass) {
    if (too_flat_to_run(cell))
        return on_glass == CellOnGlass::Flat ? RefusedFrame::Leave : RefusedFrame::FlatCell;
    return on_glass == CellOnGlass::None ? RefusedFrame::Leave : RefusedFrame::Wordmark;
}

BootPath boot_path(ResetCause causes, bool button_down, const BootCell& cell) {
    if (restarted(causes) ? below(cell, kFlatMv) : too_flat_to_run(cell))
        return BootPath::SleepAgain;
    if (button_down) return BootPath::Run;
    if (has_cause(causes, ResetCause::Pin)) return BootPath::Run;
    if (restarted(causes)) return BootPath::Run;
    // INFO: fc 12nov26 only a VBUS wake from SYSTEM OFF sets RESETREAS.VBUS: README.md
    if (has_cause(causes, ResetCause::UsbVbus)) return BootPath::SleepAgain;
    return BootPath::Run;
}

}  // namespace skyblip::power
