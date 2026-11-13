#include "core/timing/transmit.h"

#include <algorithm>
#include <initializer_list>

namespace skyblip::timing {

// The M-band dwells are wider than the direct slot at both ends: they open at
// 400 ms to catch traffic that transmits from 405, and the second one runs to
// 1200 ms to hear traffic still using its own slot there. Our own burst lives
// inside the direct slot, 450..1000 (§C.5), and completes inside both: the slot,
// because the standard says so, and the dwell, because hopping channels mid-burst
// would truncate it on air.
int Transmitter::first_instant_in(int slot) {
    const int opens = Scheduler::slot_start(slot);
    return opens > adsl::kDirectStartMs ? opens : adsl::kDirectStartMs;
}

int Transmitter::last_instant_in(int slot) {
    const int dwell_end = Scheduler::dwell_end(slot);
    const int closes = dwell_end < adsl::kDirectEndMs ? dwell_end : adsl::kDirectEndMs;
    const int last = closes - kCompletionGuardMs - static_cast<int>(kAirTimeMs);
    const int first = first_instant_in(slot);
    return last > first ? last : first;
}

int Transmitter::instant_in(int slot, uint32_t utc_s) const {
    return adsl::instant_between(address_, utc_s, first_instant_in(slot), last_instant_in(slot));
}

int Transmitter::status_instant_in(int slot, uint32_t utc_s) const {
    return adsl::status_instant_between(address_, utc_s, first_instant_in(slot),
                                        last_instant_in(slot),
                                        static_cast<int>(kAirTimeMs) + kCompletionGuardMs);
}

bool Transmitter::sent_in(uint32_t utc_s, Payload payload) const {
    const int index = static_cast<int>(payload);
    return ever_sent_[index] && utc_s == last_utc_s_[index];
}

int Transmitter::last_callsign_instant() {
    return kCallsignEndMs - kCompletionGuardMs - static_cast<int>(kAirTimeMs);
}

Transmitter::Attempt Transmitter::attempt(const SlotPlan& plan, uint32_t utc_s, uint32_t now_ms,
                                          bool airborne, int32_t fix_lag_ms) const {
    for (const Payload payload : {Payload::Position, Payload::Status, Payload::Callsign}) {
        const Attempt owed = attempt(plan, utc_s, now_ms, airborne, fix_lag_ms, payload);
        if (owed.go || owed.over_budget) return owed;
    }
    return Attempt{};
}

Transmitter::Attempt Transmitter::attempt(const SlotPlan& plan, uint32_t utc_s, uint32_t now_ms,
                                          bool airborne, int32_t fix_lag_ms,
                                          Payload payload) const {
    Attempt attempt{};
    attempt.payload = payload;
    if (!plan.tx_allowed) return attempt;
    const int dwell_slot = Scheduler::slot_of(plan.start_ms);

    if (sent_in(utc_s, payload) || !due(payload, plan, utc_s, airborne, fix_lag_ms)) return attempt;

    if (!air_time_.may_spend(now_ms, kAirTimeMs)) {
        attempt.over_budget = true;
        return attempt;
    }

    attempt.go = true;
    attempt.frequency_hz = Scheduler::slot_frequency_hz(dwell_slot);
    switch (payload) {
        case Payload::Position: attempt.at_ms = instant_in(dwell_slot, utc_s); break;
        case Payload::Status: attempt.at_ms = status_instant_in(dwell_slot, utc_s); break;
        case Payload::Callsign:
            attempt.at_ms =
                adsl::instant_between(address_, utc_s, kCallsignStartMs, last_callsign_instant());
            break;
    }
    return attempt;
}

bool Transmitter::due(Payload payload, const SlotPlan& plan, uint32_t utc_s, bool airborne,
                      int32_t fix_lag_ms) const {
    const int dwell_slot = Scheduler::slot_of(plan.start_ms);
    switch (payload) {
        case Payload::Position:
            return fix_lag_ms <= kFixMaxAgeMs && adsl::on_schedule(address_, utc_s, airborne) &&
                   dwell_slot == slot_in(utc_s, airborne);
        case Payload::Status:
            return adsl::status_due(address_, utc_s) && dwell_slot == status_slot_in(utc_s);
        case Payload::Callsign:
            return utc_s % kCallsignPeriodS == callsign_second() && dwell_slot == kCallsignSlot;
    }
    return false;
}

void Transmitter::sent(uint32_t utc_s, uint32_t now_ms, Payload payload) {
    sent_++;
    air_time_.spend(now_ms, kAirTimeMs);
    ever_sent_[static_cast<int>(payload)] = true;
    last_utc_s_[static_cast<int>(payload)] = utc_s;
}

}  // namespace skyblip::timing
