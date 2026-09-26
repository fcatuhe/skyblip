// core/power/wake.h: what the wake cause is allowed to make of the boot.
//
// The reset cause has been read, named and shown on the self-test page since
// pull request 14; nothing acted on it. This is the acting half, and it decides
// exactly one thing: whether this boot becomes a device or goes straight back to
// SYSTEM OFF. Everything here is a function of bits the platform reported, so the
// rule is host-tested and the adapter only reports and performs.
#ifndef SKYBLIP_CORE_POWER_WAKE_H
#define SKYBLIP_CORE_POWER_WAKE_H

#include <cstdint>

#include "core/power/cutoff.h"
#include "core/power/reset_reason.h"
#include "core/power/shutdown.h"

namespace skyblip::power {

// Run: bring the device up. SleepAgain: undo the wake - drop the rails and go
// back to SYSTEM OFF without painting the panel or starting the loop.
enum class BootPath : uint8_t { Run, SleepAgain };

const char* to_string(BootPath path);

// INFO: fc 21sep26 what a refused boot owes the glass, and why the refusal owes it: README.md
enum class RefusedFrame : uint8_t { Leave, Wordmark, FlatCell };

const char* to_string(RefusedFrame frame);

struct BootCell {
    uint16_t millivolts{0};
    bool valid{false};
    bool external_power{false};
};

// INFO: fc 07sep26 meshcore NRF52Board.cpp:98-126 boot-locks at 3300 mV, against over-discharge
constexpr uint16_t kBootLockoutMv = 3400;

static_assert(kCutoffMv < kBootLockoutMv,
              "a cell recovers once the load stops: the shutdown would be undone by the next "
              "press");
static_assert(kBootLockoutMv < kLowWarnMv,
              "a boot refused before the pilot has been warned is a device that reads as dead");
static_assert(kImplausibleFloorMv < kCutoffMv,
              "an unpopulated divider must fall through the lockout, not into it");

// Same shape as the two references that fly on this hardware: nrf52-ogn-tracker
// src/main.cpp:517-532 (T_Echo_StayOffOnChargerWake) and SoftRF-lyusupov
// src/platform/nRF52.cpp:944-951.
//
// INFO: fc 05aug26 charge mode was considered and refused: README.md
BootPath boot_path(ResetCause causes, bool button_down, const BootCell& cell);

ButtonWake button_wake_after_refusal(const BootCell& cell);

RefusedFrame refused_frame(const BootCell& cell, bool flat_on_glass);

}  // namespace skyblip::power

#endif
