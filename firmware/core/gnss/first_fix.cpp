#include "core/gnss/first_fix.h"

namespace skyblip::gnss {

bool FirstFix::converged(const Convergence& convergence) {
    return convergence.residual_valid && convergence.height_solved &&
           convergence.residual_m < kSettleResidualM;
}

void FirstFix::observe(const Convergence& convergence, uint32_t now_ms) {
    if (convergence.fix_valid != fix_valid_) {
        fix_valid_ = convergence.fix_valid;
        converged_fixes_ = 0;
        if (!convergence.fix_valid) return;

        settle_ms_ = ever_fixed_ ? kRefixSettleMs : kFirstFixSettleMs;
        acquired_pending_ = !ever_fixed_;
        ever_fixed_ = true;
        fix_since_ms_ = now_ms;
        return;
    }
    if (!convergence.fix_valid) return;
    if (!converged(convergence))
        converged_fixes_ = 0;
    else if (converged_fixes_ < kConvergedFixes)
        converged_fixes_++;
}

bool FirstFix::take_acquired() {
    const bool acquired = acquired_pending_;
    acquired_pending_ = false;
    return acquired;
}

bool FirstFix::settled(uint32_t now_ms) const {
    if (!fix_valid_) return false;
    if (converged_fixes_ >= kConvergedFixes) return true;
    return now_ms - fix_since_ms_ >= settle_ms_;
}

}  // namespace skyblip::gnss
