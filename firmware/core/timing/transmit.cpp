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

Window Transmitter::window_of(Payload payload, int slot) {
    const Window window = Scheduler::window(slot);
    if (payload == Payload::Callsign) return {adsl::kDirectEndMs, window.end_ms};
    return {window.start_ms, std::min(window.end_ms, adsl::kDirectEndMs)};
}

int Transmitter::last_instant(Payload payload, int slot) {
    return last_instant_ms(window_of(payload, slot), kAirTimeMs * 1000);
}

// INFO: fc 13nov26 one hash round more for a Status, two for a name, so no two bursts share a draw
int Transmitter::instant_in(Payload payload, int slot, uint32_t utc_s) const {
    uint32_t drawn = hash(address_ ^ hash(utc_s));
    for (int round = 0; round < static_cast<int>(payload); round++) drawn = hash(drawn);
    const int first = window_of(payload, slot).start_ms;
    const int last = last_instant(payload, slot);
    return first + static_cast<int>(drawn % static_cast<uint32_t>(last - first + 1));
}

uint32_t Transmitter::deferral_draw(uint64_t busy_at_us) const {
    return hash(address_ ^ hash(static_cast<uint32_t>(busy_at_us ^ (busy_at_us >> 32))));
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

uint32_t Transmitter::turn_of(Payload payload, uint32_t utc_s, bool airborne) const {
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
    const uint32_t missed = turn_of(payload, utc_s, airborne);
    if (!started_ || missed < first_s_ || missed == utc_s || (utc_s - missed) % kRetryStepS != 0 ||
        sent_since(payload, missed))
        return false;
    const int slot = channel_of(payload, missed);
    if (scheduled_on(slot, utc_s, airborne)) return false;
    for (const Payload earlier : {Payload::Position, Payload::Status}) {
        if (earlier == payload) break;
        if (retry(earlier, utc_s, airborne) &&
            channel_of(earlier, turn_of(earlier, utc_s, airborne)) == slot)
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
        const uint32_t previous = turn_of(payload, utc_s - 1, airborne);
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
    attempt.at_ms = instant_in(payload, dwell_slot, utc_s);
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
