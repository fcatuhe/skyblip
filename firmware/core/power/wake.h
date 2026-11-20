#ifndef SKYBLIP_CORE_POWER_WAKE_H
#define SKYBLIP_CORE_POWER_WAKE_H

#include <cstdint>

#include "core/power/cutoff.h"
#include "core/power/reset_reason.h"

namespace skyblip::power {

enum class BootPath : uint8_t { Run, SleepAgain };

// INFO: fc 21sep26 what a refused boot owes the glass, and why the refusal owes it: README.md
enum class RefusedFrame : uint8_t { Leave, Wordmark, FlatCell };

enum class CellOnGlass : uint8_t { None, Low, Flat };

struct BootCell {
    uint16_t cell_mv{0};
    bool valid{false};
    bool external_power{false};
};

// INFO: fc 07sep26 meshcore NRF52Board.cpp:98-126 boot-locks at 3300 mV, against over-discharge
constexpr uint16_t kBootLockoutMv = 3400;

static_assert(kFlatMv < kBootLockoutMv,
              "a cell recovers once the load stops: the shutdown would be undone by the next "
              "press");
static_assert(kBootLockoutMv < kCriticalMv,
              "a boot refused before the pilot has been warned is a device that reads as dead");
static_assert(kImplausibleFloorMv < kFlatMv,
              "an unpopulated divider must fall through the lockout, not into it");

// INFO: fc 05aug26 charge mode was considered and refused: README.md
BootPath boot_path(ResetCause causes, bool button_down, const BootCell& cell);

RefusedFrame refused_frame(const BootCell& cell, CellOnGlass on_glass);

}  // namespace skyblip::power

#endif
