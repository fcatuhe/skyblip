#include "core/gnss/validity.h"

#include <cstdlib>

#include "protocols/common/geometry.h"

namespace skyblip::gnss {

void FixValidity::reset() {
    rmc_heard_ = false;
    gga_heard_ = false;
    gsa_heard_ = false;
    rmc_fix_valid_ = false;
    gga_fix_valid_ = false;
    utc_valid_ = false;
    jumped_ = false;
    previous_valid_ = false;
}

void FixValidity::observe(const GnssSolution& solution, Sentence which, uint32_t now_ms) {
    if (which == Sentence::Gsa) {
        gsa_heard_ = true;
        gsa_heard_ms_ = now_ms;
        return;
    }
    if (which == Sentence::Gga) {
        gga_heard_ = true;
        gga_heard_ms_ = now_ms;
        gga_fix_valid_ = solution.fix_quality >= kQualityGps &&
                         solution.fix_quality <= kQualityFloatRtk && solution.altitude_msl_valid;
        return;
    }
    if (which != Sentence::Rmc) return;

    rmc_heard_ = true;
    rmc_heard_ms_ = now_ms;
    rmc_fix_valid_ = solution.fix_valid;
    utc_valid_ = solution.utc_valid;

    // INFO: fc 20nov26 losing the fix drops the reference, since a receiver that reacquires
    // elsewhere was carried there, not jumped
    if (!solution.fix_valid) {
        previous_valid_ = false;
        jumped_ = false;
        return;
    }

    const int32_t longitude_step_1e7 = wire::wrapped_longitude_1e7(
        static_cast<int64_t>(solution.longitude_1e7) - previous_longitude_1e7_);
    jumped_ = previous_valid_ &&
              (std::abs(solution.latitude_1e7 - previous_latitude_1e7_) > kLatitudeJumpCeiling1e7 ||
               std::abs(longitude_step_1e7) > kLongitudeJumpCeiling1e7);
    // INFO: fc 20nov26 a jump still becomes the reference, so one spike costs one fix, not all
    previous_latitude_1e7_ = solution.latitude_1e7;
    previous_longitude_1e7_ = solution.longitude_1e7;
    previous_valid_ = true;
}

FixReject FixValidity::check(uint32_t now_ms) {
    const FixReject reason = evaluate(now_ms);
    // INFO: fc 20nov26 a run of one reason counts once, so support reads the antenna came off
    // twice, not four thousand times
    if (reason != FixReject::None && reason != last_reject_) rejected_++;
    last_reject_ = reason;
    return reason;
}

// INFO: fc 22nov26 ADS-L 4 SRD860 issue 2 G.1.7: a 3D mode the receiver stopped reporting vouches
// for no height
void FixValidity::expire_fix_mode(GnssSolution& solution, uint32_t now_ms) const {
    if (gsa_heard_ && now_ms - gsa_heard_ms_ <= kGsaMaxAgeMs) return;
    solution.fix_mode = 0;
    solution.vdop_e2 = 0;
}

FixReject FixValidity::evaluate(uint32_t now_ms) const {
    FixReject reason = FixReject::None;
    if (!rmc_heard_)
        reason = FixReject::MissingRmc;
    else if (!gga_heard_)
        reason = FixReject::MissingGga;
    else if (now_ms - rmc_heard_ms_ > kSentenceMaxAgeMs ||
             now_ms - gga_heard_ms_ > kSentenceMaxAgeMs)
        reason = FixReject::Stale;
    else if (!rmc_fix_valid_ || !gga_fix_valid_)
        reason = FixReject::NoSolution;
    else if (!utc_valid_)
        reason = FixReject::NoDate;
    else if (jumped_)
        reason = FixReject::Jump;
    return reason;
}

}  // namespace skyblip::gnss
