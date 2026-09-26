#include "core/gnss/validity.h"

#include "core/util/intmath.h"

namespace skyblip::gnss {

namespace {
int32_t magnitude(int32_t v) { return v < 0 ? -v : v; }
}

const char* reject_name(FixReject reason) {
    switch (reason) {
        case FixReject::NoSolution: return "NO SOLUTION";
        case FixReject::MissingRmc: return "NO RMC";
        case FixReject::MissingGga: return "NO GGA";
        case FixReject::Stale: return "STALE";
        case FixReject::NoDate: return "NO DATE";
        case FixReject::Jump: return "JUMP";
        case FixReject::None: break;
    }
    return "NONE";
}

void FixValidity::reset() {
    rmc_heard_ = false;
    gga_heard_ = false;
    rmc_solution_ = false;
    gga_solution_ = false;
    date_ok_ = false;
    jumped_ = false;
    previous_valid_ = false;
}

void FixValidity::observe(const GnssSolution& solution, Sentence which, uint32_t now_ms) {
    if (which == Sentence::Gga) {
        gga_heard_ = true;
        gga_ms_ = now_ms;
        gga_solution_ = solution.fix_quality >= kQualityGps &&
                        solution.fix_quality <= kQualityFloatRtk && solution.alt_msl_valid;
        return;
    }
    if (which != Sentence::Rmc) return;

    rmc_heard_ = true;
    rmc_ms_ = now_ms;
    rmc_solution_ = solution.fix_valid;
    date_ok_ = solution.utc_valid;

    // The jump gate compares consecutive SOLUTIONS, so losing the fix drops the
    // reference: a receiver that reacquires somewhere else has not jumped, it
    // has been switched off in a car. moshe-braner keeps the stale reference and
    // eats one bad fix on reacquisition; we would rather not transmit one.
    if (!solution.fix_valid) {
        previous_valid_ = false;
        jumped_ = false;
        return;
    }

    const int32_t lon_step_1e7 =
        wrapped_lon_1e7(static_cast<int64_t>(solution.lon_1e7) - prev_lon_1e7_);
    jumped_ =
        previous_valid_ && (magnitude(solution.lat_1e7 - prev_lat_1e7_) > kMaxLatitudeJump1e7 ||
                            magnitude(lon_step_1e7) > kMaxLongitudeJump1e7);
    // The new position becomes the reference either way: one implausible step
    // costs one fix, not every fix after it. Two receivers disagreeing about
    // where we are is a stuck state; a single spike is a spike.
    prev_lat_1e7_ = solution.lat_1e7;
    prev_lon_1e7_ = solution.lon_1e7;
    previous_valid_ = true;
}

FixReject FixValidity::check(uint32_t now_ms) {
    const FixReject reason = evaluate(now_ms);
    // A run of refusals for the same reason is one event, not one per poll: the
    // counter support reads should say "the antenna came off twice", not "the
    // antenna came off four thousand times".
    if (reason != FixReject::None && reason != last_reject_) rejected_++;
    last_reject_ = reason;
    return reason;
}

FixReject FixValidity::evaluate(uint32_t now_ms) const {
    FixReject reason = FixReject::None;
    if (!rmc_heard_)
        reason = FixReject::MissingRmc;
    else if (!gga_heard_)
        reason = FixReject::MissingGga;
    else if (now_ms - rmc_ms_ > kSentenceMaxAgeMs || now_ms - gga_ms_ > kSentenceMaxAgeMs)
        reason = FixReject::Stale;
    else if (!rmc_solution_ || !gga_solution_)
        reason = FixReject::NoSolution;
    else if (!date_ok_)
        reason = FixReject::NoDate;
    else if (jumped_)
        reason = FixReject::Jump;
    return reason;
}

}  // namespace skyblip::gnss
