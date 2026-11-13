#include "core/timing/transmit.h"

#include <algorithm>
#include <initializer_list>
#include <iterator>

namespace skyblip::timing {

namespace {
uint32_t hash(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
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
    const int first = first_instant_in(slot);
    const int last = last_instant_in(slot);
    if (last <= first) return first;
    // Inclusive of last: a burst starting there still ends inside the slack.
    return first +
           static_cast<int>(hash(address_ ^ hash(utc_s)) % static_cast<uint32_t>(last - first + 1));
}

// INFO: fc 13nov26 one hash round more for a Status, two for a name, so no two extras share a draw
int Transmitter::extra_instant_in(int slot, uint32_t utc_s, Payload payload) const {
    uint32_t drawn = hash(hash(address_ ^ hash(utc_s)));
    int first = first_instant_in(slot);
    int last = last_instant_in(slot);
    if (payload == Payload::Callsign) {
        drawn = hash(drawn);
        first = kCallsignStartMs;
        last = last_callsign_instant();
    }
    if (last <= first) return first;
    return first + static_cast<int>(drawn % static_cast<uint32_t>(last - first + 1));
}

int Transmitter::last_callsign_instant() {
    return kCallsignEndMs - kCompletionGuardMs - static_cast<int>(kAirTimeMs);
}

uint32_t Transmitter::cycle_start() const {
    return 2 * ((address_ & settings::kSerialMask) % adsl::kGroundReportPeriodS);
}

uint32_t Transmitter::cycle_second(uint32_t utc_s) const {
    return (utc_s % kCycleS + kCycleS - cycle_start()) % kCycleS;
}

int Transmitter::channel_of(Payload payload, uint32_t utc_s) {
    switch (payload) {
        case Payload::Position: return slot_in(utc_s);
        case Payload::Status: return extra_slot_in(utc_s);
        case Payload::Callsign: return kCallsignSlot;
    }
    return 0;
}

bool Transmitter::turn(Payload payload, uint32_t utc_s, bool airborne) const {
    const uint32_t second = cycle_second(utc_s);
    const uint32_t* offsets = kPositionOffsetsS;
    switch (payload) {
        case Payload::Position:
            if (airborne) return true;
            break;
        case Payload::Status: offsets = kStatusOffsetsS; break;
        case Payload::Callsign: offsets = kCallsignOffsetsS; break;
    }
    return second == offsets[0] || second == offsets[1];
}

uint32_t Transmitter::latest_turn(Payload payload, uint32_t utc_s, bool airborne) const {
    for (uint32_t back = 0; back < kCycleS; back++)
        if (turn(payload, utc_s - back, airborne)) return utc_s - back;
    return utc_s;
}

bool Transmitter::scheduled_on(int slot, uint32_t utc_s, bool airborne) const {
    constexpr Payload kEvery[] = {Payload::Position, Payload::Status, Payload::Callsign};
    return std::any_of(std::begin(kEvery), std::end(kEvery), [&](Payload payload) {
        return turn(payload, utc_s, airborne) && channel_of(payload, utc_s) == slot;
    });
}

// INFO: fc 13nov26 a retry takes a dwell no turn of ours holds, and yields to a retry listed first
bool Transmitter::retry(Payload payload, uint32_t utc_s, bool airborne) const {
    const uint32_t missed = latest_turn(payload, utc_s, airborne);
    if (!started_ || missed < first_s_ || missed == utc_s || (utc_s - missed) % kRetryStepS != 0 ||
        sent_since(payload, missed))
        return false;
    const int slot = channel_of(payload, missed);
    if (scheduled_on(slot, utc_s, airborne)) return false;
    for (const Payload earlier : {Payload::Position, Payload::Status}) {
        if (earlier == payload) break;
        if (retry(earlier, utc_s, airborne) &&
            channel_of(earlier, latest_turn(earlier, utc_s, airborne)) == slot)
            return false;
    }
    return true;
}

void Transmitter::start(uint32_t utc_s) {
    if (started_) return;
    started_ = true;
    first_s_ = utc_s;
}

uint32_t Transmitter::give_up(uint32_t utc_s, bool airborne, bool named) {
    start(utc_s);
    if (utc_s == closed_s_) return 0;
    const bool first = closed_s_ == 0;
    closed_s_ = utc_s;
    if (first) return 0;
    uint32_t given_up = 0;
    for (const Payload payload : {Payload::Position, Payload::Status, Payload::Callsign}) {
        if (payload == Payload::Callsign && !named) continue;
        if (!turn(payload, utc_s, airborne)) continue;
        const uint32_t previous = latest_turn(payload, utc_s - 1, airborne);
        if (previous >= first_s_ && !sent_since(payload, previous)) given_up++;
    }
    return given_up;
}

bool Transmitter::sent_since(Payload payload, uint32_t utc_s) const {
    const int index = static_cast<int>(payload);
    return ever_sent_[index] && last_utc_s_[index] >= utc_s;
}

bool Transmitter::sent_in(uint32_t utc_s, Payload payload) const {
    const int index = static_cast<int>(payload);
    return ever_sent_[index] && utc_s == last_utc_s_[index];
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
        case Payload::Status:
        case Payload::Callsign: attempt.at_ms = extra_instant_in(dwell_slot, utc_s, payload); break;
    }
    return attempt;
}

bool Transmitter::due(Payload payload, const SlotPlan& plan, uint32_t utc_s, bool airborne,
                      int32_t fix_lag_ms) const {
    const int dwell_slot = Scheduler::slot_of(plan.start_ms);
    if (payload == Payload::Position && fix_lag_ms > kFixMaxAgeMs) return false;
    if (dwell_slot != channel_of(payload, utc_s)) return false;
    return turn(payload, utc_s, airborne) || retry(payload, utc_s, airborne);
}

void Transmitter::sent(uint32_t utc_s, uint32_t now_ms, Payload payload) {
    start(utc_s);
    sent_++;
    air_time_.spend(now_ms, kAirTimeMs);
    ever_sent_[static_cast<int>(payload)] = true;
    last_utc_s_[static_cast<int>(payload)] = utc_s;
}

}  // namespace skyblip::timing
