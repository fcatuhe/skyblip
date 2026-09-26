#include "core/gnss/first_fix.h"

namespace skyblip::gnss {

bool FirstFix::converged(const Convergence& solution) {
    return solution.resid_valid && solution.height_solved && solution.resid_m < kSettleResidualM;
}

void FirstFix::update(const Convergence& solution, uint32_t now_ms) {
    if (solution.fix_valid != fix_valid_) {
        fix_valid_ = solution.fix_valid;
        converged_ = 0;
        if (!solution.fix_valid) return;

        settle_ms_ = ever_fixed_ ? kRefixSettleMs : kFirstFixSettleMs;
        acquired_pending_ = !ever_fixed_;
        ever_fixed_ = true;
        fix_since_ms_ = now_ms;
        return;
    }
    if (!solution.fix_valid) return;
    if (!converged(solution))
        converged_ = 0;
    else if (converged_ < kConvergedFixes)
        converged_++;
}

bool FirstFix::take_acquired() {
    const bool acquired = acquired_pending_;
    acquired_pending_ = false;
    return acquired;
}

bool FirstFix::settled(uint32_t now_ms) const {
    if (!fix_valid_) return false;
    if (converged_ >= kConvergedFixes) return true;
    return now_ms - fix_since_ms_ >= settle_ms_;
}

}  // namespace skyblip::gnss
