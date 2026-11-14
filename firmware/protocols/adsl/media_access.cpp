#include "protocols/adsl/media_access.h"

#include <algorithm>

#include "protocols/common/en300220.h"

namespace skyblip::adsl {

AccessVerdict media_access(const ChannelAttempt& attempt, const Deferral& deferral) {
    if (attempt.forced_end_us && attempt.at_us < *attempt.forced_end_us + kSilenceAfterForceUs)
        return {Access::Silent, 0};
    if (attempt.clear) return {Access::Transmit, attempt.at_us};
    if (attempt.last_us <= attempt.at_us) return {Access::Missed, 0};

    const uint64_t unit_us = std::max<uint64_t>(deferral.unit_us, en300220::kDeferralFloorUs);
    const uint64_t wait_us = std::max(attempt.earliest_us, attempt.at_us + unit_us) - attempt.at_us;
    const uint64_t first_units = (wait_us + unit_us - 1) / unit_us;
    const uint64_t last_units = (attempt.last_us - attempt.at_us) / unit_us;
    if (last_units < first_units) return {Access::Missed, 0};

    const bool waited = attempt.at_us >= attempt.first_us &&
                        attempt.at_us - attempt.first_us >= kForceAfterAttemptUs;
    if (attempt.may_force && waited) return {Access::Force, attempt.at_us + first_units * unit_us};

    const uint64_t most_units =
        std::min<uint64_t>(std::max<uint32_t>(deferral.units, 1), last_units);
    if (most_units < first_units) return {Access::Missed, 0};
    const uint64_t units = first_units + deferral.draw % (most_units - first_units + 1);
    return {Access::Defer, attempt.at_us + units * unit_us};
}

}  // namespace skyblip::adsl
