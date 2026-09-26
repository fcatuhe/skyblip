// core/gnss/first_fix.h: the moment the receiver first solves, and the quiet
// period that follows it. Two separate questions live here: what the pilot is
// told (once, on the first fix ever) and when own-ship state is steady enough to
// be worth transmitting.
#ifndef SKYBLIP_CORE_GNSS_FIRST_FIX_H
#define SKYBLIP_CORE_GNSS_FIRST_FIX_H

#include <cstdint>

#include "core/flight/state.h"

namespace skyblip::gnss {

// INFO: hk 02aug26 the moshe-braner SoftRF fork holds transmission for 20 s
// after the first fix and 5 s after a re-fix (SoftRF.ino:552-580). A cold
// receiver's first solutions walk: position, altitude and above all ground
// speed settle over the seconds that follow, and the flight state we derive
// from speed decides our transmit rate. Transmitting through that window
// publishes a track that nobody flew.
constexpr uint32_t kFirstFixSettleMs = 20000;
constexpr uint32_t kRefixSettleMs = 5000;

// INFO: fc 18sep26 the takeoff speed over the second a residual spans, so no smaller error invents
// a flight
constexpr uint16_t kSettleResidualM = flight::kFlightSpeedMmS / 1000;
constexpr uint8_t kConvergedFixes = 3;

struct Convergence {
    bool fix_valid{false};
    bool resid_valid{false};
    bool height_solved{false};
    uint16_t resid_m{0};
};

class FirstFix {
   public:
    void update(const Convergence& solution, uint32_t now_ms);

    // True once, on the first valid fix since boot. The caller that annunciates
    // consumes it, so nothing can chirp twice.
    bool take_acquired();

    bool ever_fixed() const { return ever_fixed_; }
    bool has_fix() const { return fix_valid_; }
    uint32_t fix_since_ms() const { return fix_since_ms_; }

    bool settled(uint32_t now_ms) const;

    uint8_t converged_fixes() const { return converged_; }

   private:
    static bool converged(const Convergence& solution);

    uint32_t fix_since_ms_{0};
    uint32_t settle_ms_{kFirstFixSettleMs};
    uint8_t converged_{0};
    bool fix_valid_{false};
    bool ever_fixed_{false};
    bool acquired_pending_{false};
};

}  // namespace skyblip::gnss

#endif
