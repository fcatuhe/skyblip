#ifndef SKYBLIP_CORE_GNSS_FIRST_FIX_H
#define SKYBLIP_CORE_GNSS_FIRST_FIX_H

#include <cstdint>

#include "core/flight/state.h"

namespace skyblip::gnss {

// INFO: fc 20nov26 the moshe-braner SoftRF fork waits 20 s after a first fix and 5 s after a re-fix
// (SoftRF.ino:552-580)
constexpr uint32_t kFirstFixSettleMs = 20000;
constexpr uint32_t kRefixSettleMs = 5000;

// INFO: fc 18sep26 the takeoff speed over the second a residual spans, so no smaller error invents
// a flight
constexpr uint16_t kSettleResidualM = flight::kFlightSpeedMmS / 1000;
constexpr uint8_t kConvergedFixes = 3;

struct Convergence {
    bool fix_valid{false};
    bool residual_valid{false};
    bool height_solved{false};
    uint16_t residual_m{0};
};

class FirstFix {
   public:
    void observe(const Convergence& convergence, uint32_t now_ms);

    bool take_acquired();

    bool ever_fixed() const { return ever_fixed_; }
    uint32_t fix_since_ms() const { return fix_since_ms_; }

    bool settled(uint32_t now_ms) const;

    uint8_t converged_fixes() const { return converged_fixes_; }

   private:
    static bool converged(const Convergence& convergence);

    uint32_t fix_since_ms_{0};
    uint32_t settle_ms_{kFirstFixSettleMs};
    uint8_t converged_fixes_{0};
    bool fix_valid_{false};
    bool ever_fixed_{false};
    bool acquired_pending_{false};
};

}  // namespace skyblip::gnss

#endif
