#include "core/timing/transmit.h"

namespace skyblip::timing {

namespace {
uint32_t mix(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
}  // namespace

// The M-band dwells are wider than the direct slot at both ends: they open at
// 400 ms to catch traffic that transmits from 405, and the second one runs to
// 1200 ms to hear traffic still using its own slot there. Our own burst lives
// inside the direct slot, 450..1000 (§C.5), and completes inside both: the slot,
// because the standard says so, and the dwell, because hopping channels mid-burst
// would truncate it on air.
int Transmitter::first_instant_in(int slot) {
    const int opens = Scheduler::slot_start(slot);
    return opens > kDirectStart ? opens : kDirectStart;
}

int Transmitter::last_instant_in(int slot) {
    const int slot_end = Scheduler::slot_end(slot);
    const int closes = slot_end < kDirectEnd ? slot_end : kDirectEnd;
    const int last = closes - kCompletionSlackMs - static_cast<int>(kAirTimeMs);
    const int first = first_instant_in(slot);
    return last > first ? last : first;
}

int Transmitter::instant_in(int slot, uint32_t utc) const {
    return instant_between(first_instant_in(slot), last_instant_in(slot), utc);
}

int Transmitter::instant_between(int first, int last, uint32_t utc) const {
    if (last <= first) return first;
    // Inclusive of last: a burst starting there still ends inside the slack.
    return first +
           static_cast<int>(mix(addr_ ^ mix(utc)) % static_cast<uint32_t>(last - first + 1));
}

uint32_t Transmitter::ground_second() const { return mix(addr_) % kGroundPeriodS; }

uint32_t Transmitter::callsign_second() const {
    const uint32_t ground = ground_second();
    return ground % 2 == 0 ? (ground + 2) % kCallsignPeriodS : ground - 1;
}

bool Transmitter::on_schedule(uint32_t utc, bool airborne) const {
    return utc % period_s(airborne) == (airborne ? 0u : ground_second());
}

bool Transmitter::spoke_in(uint32_t utc) const { return ever_sent_ && utc == last_sent_utc_; }

bool Transmitter::named_in(uint32_t utc) const { return ever_named_ && utc == last_callsign_utc_; }

bool Transmitter::on_callsign_schedule(uint32_t utc) const {
    return utc % kCallsignPeriodS == callsign_second();
}

int Transmitter::last_callsign_instant() {
    return kCallsignEnd - kCompletionSlackMs - static_cast<int>(kAirTimeMs);
}

Transmitter::Attempt Transmitter::attempt(const SlotPlan& plan, uint32_t utc, uint32_t now_ms,
                                          bool airborne, int32_t fix_lag_ms) const {
    Attempt a{};
    if (!plan.tx_allowed) return a;
    const int dwell_slot = Scheduler::slot_of(plan.start_ms);

    const bool position_due = !spoke_in(utc) && fix_lag_ms <= kFixLagMaxMs &&
                              on_schedule(utc, airborne) && dwell_slot == slot_in(utc, airborne);
    const bool callsign_due =
        !named_in(utc) && on_callsign_schedule(utc) && dwell_slot == kCallsignSlot;
    if (!position_due && !callsign_due) return a;

    if (!air_.may_spend(now_ms, kAirTimeMs)) {
        a.over_budget = true;
        return a;
    }

    a.go = true;
    if (position_due) {
        const int slot = slot_in(utc, airborne);
        a.payload = Payload::Position;
        a.at_ms = instant_in(slot, utc);
        a.freq_hz = Scheduler::slot_freq(slot);
        return a;
    }
    a.payload = Payload::Callsign;
    a.at_ms = instant_between(kCallsignStart, last_callsign_instant(), utc);
    a.freq_hz = Scheduler::slot_freq(kCallsignSlot);
    return a;
}

void Transmitter::sent(uint32_t utc, uint32_t now_ms, Payload payload) {
    sent_++;
    air_.spend(now_ms, kAirTimeMs);
    if (payload == Payload::Callsign) {
        ever_named_ = true;
        last_callsign_utc_ = utc;
        return;
    }
    ever_sent_ = true;
    last_sent_utc_ = utc;
}

}  // namespace skyblip::timing
