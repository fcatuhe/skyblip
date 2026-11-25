#include "products/skyblip_go/services/radio.h"

#include <algorithm>
#include <iterator>
#include <limits>

#include "core/model/ownship.h"

namespace skyblip::go {

Status RadioService::setup() {
    if (!ports::has(context_.roles.capabilities, ports::Capability::Rf)) return Status::Down;
    pass_us_ = context_.roles.clock.microseconds();
    if (!identified()) return Status::Ok;
    arm_dwell(timing::Scheduler::plan(phase_ms(), context_.state.clock), dwell_origin_us(), 0,
              Role::Current);
    return Status::Ok;
}

// INFO: fc 20nov26 the executor keeps the edge between dwells, since a pass notices it only after
void RadioService::tick(uint32_t now_ms) {
    pass_us_ = context_.roles.clock.microseconds();
    judge_transmit(now_ms);
    if (!identified()) return;
    accrue_armed();
    const timing::SlotPlan plan = timing::Scheduler::plan(phase_ms(), context_.state.clock);
    context_.state.rf.plan = plan;
    count_given_up(now_ms);
    take_carrier_samples(now_ms);
    take_switching(now_ms);
    collect_outcome(now_ms);
    collect_busy(now_ms);

    if (next_armed_ && pass_us_ >= next_dwell_.from_us) promote();
    const int64_t origin_us = dwell_origin_us();
    count_held_for_fix(plan, origin_us, now_ms);
    // INFO: fc 17nov26 a retune plans the dwell it leads to, which is the queued one
    if (next_armed_ && holds(next_dwell_, plan)) {
        publish_dwell(now_ms);
        return;
    }
    if (!holds(current_dwell_, plan)) {
        if (current_dwell_.mode != ports::RfMode::Idle) context_.roles.rf.abort();
        next_armed_ = false;
        arm_dwell(plan, origin_us, now_ms, Role::Current);
    } else if (transmit_due(plan, origin_us, now_ms)) {
        arm_dwell(plan, origin_us, now_ms, Role::Current);
    }
    queue_next(plan, origin_us, now_ms);
    publish_dwell(now_ms);
}

uint32_t RadioService::due_in_ms(uint32_t /*now_ms*/) const {
    if (!ports::has(context_.roles.capabilities, ports::Capability::Rf) ||
        !context_.state.activation.address())
        return runtime::kServiceRestCeilingMs;
    if (!next_armed_) return runtime::kServicePassCeilingMs;
    const uint64_t now_us = context_.roles.clock.microseconds();
    if (next_dwell_.from_us <= now_us) return 0;
    const uint64_t until_ms = (next_dwell_.from_us - now_us + 999) / 1000;
    return static_cast<uint32_t>(std::min<uint64_t>(until_ms, runtime::kServiceRestCeilingMs));
}

// INFO: fc 18oct26 a unit with no serial arms nothing, so it neither transmits nor receives
// | 14nov26 the schedule keys on the serial, so setting an aircraft address moves no burst
bool RadioService::identified() {
    const settings::AirAddress address = context_.state.activation.address();
    if (address) transmitter_.configure(*address);
    return address.has_value();
}

// INFO: fc 20nov26 the one place the dwell phase is published: flash writes read it, never redo it
void RadioService::publish_dwell(uint32_t now_ms) {
    timing::DwellPhase& dwell = context_.state.rf.dwell;
    // INFO: fc 03oct26 one clock read: milliseconds() is the low 32 bits of microseconds() / 1000
    const uint64_t read_us = context_.roles.clock.microseconds();
    dwell.at_ms = static_cast<uint32_t>(read_us / 1000);
    dwell.phase_ms = phase_at(read_us);
    dwell.armed = current_dwell_.mode != ports::RfMode::Idle;
    dwell.burst_armed = current_dwell_.carries_any();
    context_.state.rf.noise_dbm = noise_floor_.level_dbm();
    context_.state.rf.duty_permille = duty_permille(now_ms);
}

void RadioService::accrue_armed() {
    const uint64_t now_us = context_.roles.clock.microseconds();
    armed_us_ += armed_between(current_dwell_, accounted_us_, now_us);
    if (next_armed_) armed_us_ += armed_between(next_dwell_, accounted_us_, now_us);
    accounted_us_ = now_us;
    context_.state.duty.rx_armed_ms = static_cast<uint32_t>(armed_us_ / 1000);
}

uint64_t RadioService::armed_between(const Armed& dwell, uint64_t from_us, uint64_t to_us) {
    return std::clamp(to_us, dwell.from_us, dwell.until_us) -
           std::clamp(from_us, dwell.from_us, dwell.until_us);
}

// INFO: fc 27sep26 one read per pass: a plan and a promotion read apart straddled the 800 edge
int RadioService::phase_ms() const { return phase_at(pass_us_); }

int RadioService::phase_at(uint64_t now_us) const {
    const timing::ClockState& clock = context_.state.clock;
    if (timing::pps_anchored(clock) && now_us >= clock.pps_edge_us)
        return static_cast<int>((now_us - clock.pps_edge_us) / 1000 % 1000);
    return static_cast<int>(now_us / 1000 % 1000);
}

int64_t RadioService::second_origin_us() const {
    const timing::ClockState& clock = context_.state.clock;
    const uint64_t now_us = pass_us_;
    if (timing::pps_anchored(clock) && now_us >= clock.pps_edge_us)
        return static_cast<int64_t>(clock.pps_edge_us +
                                    (now_us - clock.pps_edge_us) / kSecondUs * kSecondUs);
    return static_cast<int64_t>(now_us - now_us % kSecondUs);
}

// INFO: fc 20nov26 signed, since slot 1's tail in the first second began before the clock did
int64_t RadioService::dwell_origin_us() const {
    const int64_t origin_us = second_origin_us();
    const bool in_spill = phase_ms() < timing::kWindowBSpillMs;
    return in_spill ? origin_us - static_cast<int64_t>(kSecondUs) : origin_us;
}

uint64_t RadioService::at_us(int64_t origin_us, int phase_ms) {
    return static_cast<uint64_t>(std::max<int64_t>(0, origin_us + int64_t{phase_ms} * 1000));
}

RadioService::Upcoming RadioService::upcoming_after(const timing::SlotPlan& plan,
                                                    int64_t origin_us) const {
    const timing::ClockState& clock = context_.state.clock;
    switch (timing::Scheduler::slot_of(plan.start_ms)) {
        case 0: return {timing::Scheduler::plan(timing::kWindowBMs.start_ms, clock), origin_us};
        case 1:
            return {timing::Scheduler::plan(timing::kUplinkWindowMs.start_ms, clock),
                    origin_us + static_cast<int64_t>(kSecondUs)};
        default: return {timing::Scheduler::plan(timing::kWindowAMs.start_ms, clock), origin_us};
    }
}

bool RadioService::holds(const Armed& dwell, const timing::SlotPlan& plan) {
    return dwell.mode == mode_for(plan) && dwell.frequency_hz == plan.frequency_hz;
}

uint64_t RadioService::instant_us(const timing::Transmitter::Attempt& attempt, int64_t origin_us) {
    return at_us(origin_us, attempt.at_ms);
}

void RadioService::promote() {
    fly(next_dwell_);
    next_armed_ = false;
}

void RadioService::fly(const Armed& dwell) {
    if (current_dwell_.carries_any()) {
        miss(retired_dwell_);
        retired_dwell_ = current_dwell_;
    }
    current_dwell_ = dwell;
}

uint32_t RadioService::slot_utc_s(uint32_t now_ms) const {
    const uint32_t utc_s = context_.state.traffic_now_s(now_ms);
    const bool in_spill = phase_ms() < timing::kWindowBSpillMs;
    return (in_spill && utc_s > 0) ? utc_s - 1 : utc_s;
}

ports::RfMode RadioService::mode_for(const timing::SlotPlan& plan) {
    return plan.band == timing::Band::O ? ports::RfMode::RxOBand : ports::RfMode::RxMBand;
}

// INFO: fc 20nov26 a dwell sets its modulation too, since §C.4 runs at twice §C.2's chip rate
void RadioService::listen_for(timing::Band band, ports::RfPlan& plan) {
    if (band == timing::Band::O) {
        plan.sync = adsl::kUplinkSync;
        plan.sync_bits = adsl::kUplinkSyncBits;
        plan.rx_bytes = 1 + adsl::kUplinkMaxLength;
        plan.bitrate = adsl::kUplinkChipRateBps;
        plan.deviation_hz = adsl::kUplinkDeviationHz;
        plan.bandwidth_hz = adsl::kUplinkChannelBandwidthHz;
        plan.gaussian_bt_e2 = adsl::kUplinkGaussianBtE2;
        return;
    }
    plan.sync = protocol::kSharedSync;
    plan.sync_bits = protocol::kSharedSyncBits;
    plan.rx_bytes = protocol::kRxChipBytes;
    plan.tx_sync = protocol::kAdslTxSync;
    plan.tx_sync_bits = protocol::kAdslTxSyncBits;
    plan.tx_preamble_chips = protocol::kAdslTxPreambleChips;
    plan.bitrate = adsl::kMBandChipRateBps;
    plan.deviation_hz = adsl::kMBandDeviationHz;
    plan.bandwidth_hz = adsl::kMBandChannelBandwidthHz;
    plan.gaussian_bt_e2 = adsl::kMBandGaussianBtE2;
}

// INFO: fc 15sep26 the gates a burst waits on clear inside the dwell, ports::Rf::arm() joins it
// there
bool RadioService::transmit_due(const timing::SlotPlan& plan, int64_t origin_us,
                                uint32_t now_ms) const {
    return owes(current_dwell_, plan, origin_us, now_ms);
}

bool RadioService::owes(const Armed& dwell, const timing::SlotPlan& plan, int64_t origin_us,
                        uint32_t now_ms) const {
    return std::any_of(std::begin(kEveryPayload), std::end(kEveryPayload), [&](Payload payload) {
        if (dwell.carries(payload)) return false;
        const timing::Transmitter::Attempt payload_attempt =
            attempt(plan, origin_us, now_ms, payload);
        const uint64_t tx_at_us = instant_us(payload_attempt, origin_us);
        return payload_attempt.go && tx_at_us > context_.roles.clock.microseconds() &&
               !silenced(tx_at_us);
    });
}

// INFO: fc 20nov26 queued bare, then again with its burst on the pass its gates clear
void RadioService::queue_next(const timing::SlotPlan& plan, int64_t origin_us, uint32_t now_ms) {
    if (current_dwell_.mode == ports::RfMode::Idle) return;
    const Upcoming next = upcoming_after(plan, origin_us);
    if (next_armed_ && !owes(next_dwell_, next.plan, next.origin_us, now_ms)) return;
    arm_dwell(next.plan, next.origin_us, now_ms, Role::Next);
}

timing::Transmitter::Attempt RadioService::attempt(const timing::SlotPlan& plan, int64_t origin_us,
                                                   uint32_t now_ms) const {
    timing::Transmitter::Attempt held{};
    for (const Payload payload : kEveryPayload) {
        const timing::Transmitter::Attempt owed = attempt(plan, origin_us, now_ms, payload);
        if (owed.go || owed.over_budget) return owed;
        if (owed.stale) held = owed;
    }
    return held;
}

timing::Transmitter::Attempt RadioService::attempt(const timing::SlotPlan& plan, int64_t origin_us,
                                                   uint32_t now_ms, Payload payload) const {
    const model::Ownship& own = context_.state.own;
    if (!context_.state.transmit.transmits()) return timing::Transmitter::Attempt{};
    if (payload == Payload::Callsign && settings_.callsign[0] == 0)
        return timing::Transmitter::Attempt{};
    timing::Transmitter::Attempt owed =
        transmitter_.attempt(plan, slot_utc_s(now_ms), now_ms, full_rate(), fix_lag_ms(origin_us),
                             payload, fix_in_hand_ms(origin_us));
    // INFO: fc 18nov26 a fix no timely fix anchored has no age a position can be judged by
    if (payload == Payload::Position && owed.go && !own.fix_dated) {
        owed.go = false;
        owed.stale = true;
    }
    return owed;
}

bool RadioService::full_rate() const {
    const model::Ownship& own = context_.state.own;
    return !adsl::reduced_rate(flight::announced_state(own.flight_state, own.aircraft_category));
}

void RadioService::count_given_up(uint32_t now_ms) {
    if (!context_.state.transmit.transmits()) return;
    context_.state.air.tx_given_up +=
        transmitter_.give_up(slot_utc_s(now_ms), full_rate(), settings_.callsign[0] != 0);
}

// INFO: fc 19nov26 a position seen waiting for its fix is lost once its second ends unarmed
void RadioService::count_held_for_fix(const timing::SlotPlan& plan, int64_t origin_us,
                                      uint32_t now_ms) {
    const uint32_t utc_s = slot_utc_s(now_ms);
    if (fix_wanted_ && fix_wanted_s_ != utc_s) {
        fix_wanted_ = false;
        hold_for_fix(fix_wanted_s_);
    }
    if (attempt(plan, origin_us, now_ms, Payload::Position).stale) await_fix(utc_s);
}

void RadioService::await_fix(uint32_t utc_s) {
    fix_wanted_ = true;
    fix_wanted_s_ = utc_s;
}

void RadioService::carry_position(const timing::Transmitter::Attempt& position, uint32_t utc_s) {
    if (fix_wanted_ && fix_wanted_s_ == utc_s) fix_wanted_ = false;
    if (!position.redrawn || (redrawn_ && redrawn_s_ == utc_s)) return;
    context_.state.air.tx_redrawn_for_fix++;
    redrawn_ = true;
    redrawn_s_ = utc_s;
}

void RadioService::hold_for_fix(uint32_t utc_s) {
    if (held_for_fix_ && held_for_fix_s_ == utc_s) return;
    context_.state.air.tx_held_for_fix++;
    held_for_fix_ = true;
    held_for_fix_s_ = utc_s;
}

// INFO: fc 18nov26 measured from the second the dwell is armed in, negative for a quarter inside it
int32_t RadioService::fix_lag_ms(int64_t origin_us) const {
    const uint32_t second_ms = static_cast<uint32_t>(at_us(origin_us, 0) / 1000);
    return static_cast<int32_t>(second_ms - context_.state.own.fix_at_ms);
}

int32_t RadioService::fix_in_hand_ms(int64_t origin_us) const {
    const uint32_t second_ms = static_cast<uint32_t>(at_us(origin_us, 0) / 1000);
    return static_cast<int32_t>(context_.state.own.fix_in_hand_ms - second_ms);
}

void RadioService::arm_dwell(const timing::SlotPlan& slot, int64_t origin_us, uint32_t now_ms,
                             Role role) {
    const int phase = phase_ms();
    const uint64_t now_us = context_.roles.clock.microseconds();

    ports::RfPlan plan{};
    plan.mode = mode_for(slot);
    plan.frequency_hz = slot.frequency_hz;
    // INFO: fc 20nov26 the unit's reference trim applies to every dwell, receive and transmit alike
    plan.frequency_correction_e1_ppm = settings_.frequency_trim_e1_ppm;
    listen_for(slot.band, plan);
    plan.end_us = at_us(origin_us, slot.end_ms);
    if (plan.end_us <= now_us) return;
    // INFO: fc 20nov26 a dwell armed late begins now and keeps its stop, rather than wait a second
    const uint64_t opens_us = at_us(origin_us, slot.start_ms);
    plan.start_us = role == Role::Next ? opens_us : std::max(opens_us, now_us);

    const timing::Transmitter::Attempt first = attempt(slot, origin_us, now_ms);
    over_budget_ = first.over_budget;
    // INFO: fc 20nov26 the budget's refusal counts apart from a dwell armed that missed its outcome
    if (first.over_budget) {
        context_.state.rf.timing_statistics.record_refused();
        if (!held_logged_) {
            log_refusal(radio::Verdict::Held, slot, now_ms);
            held_logged_ = true;
        }
    }

    const Armed& current = role == Role::Next ? next_dwell_ : current_dwell_;
    const bool rearmed = (role == Role::Current || next_armed_) && current.mode == plan.mode &&
                         current.frequency_hz == plan.frequency_hz &&
                         current.until_us == plan.end_us;
    Armed armed{};
    if (rearmed) {
        armed = current;
    } else {
        armed.mode = plan.mode;
        armed.frequency_hz = plan.frequency_hz;
        armed.from_us = plan.start_us;
        armed.until_us = plan.end_us;
        armed.origin_us = origin_us;
        armed.utc_s = slot_utc_s(now_ms);
        armed.chip_buffer = role == Role::Next
                                ? static_cast<uint8_t>(current_dwell_.chip_buffer ^ 1u)
                                : current_dwell_.chip_buffer;
    }

    bool carried_now = false;
    bool dwell_armed = rearmed;
    for (const Payload payload : kEveryPayload) {
        if (armed.carries(payload)) continue;
        const timing::Transmitter::Attempt payload_attempt =
            attempt(slot, origin_us, now_ms, payload);
        const uint64_t tx_at_us = instant_us(payload_attempt, origin_us);
        if (!payload_attempt.go || tx_at_us < std::max(plan.start_us, now_us) ||
            tx_at_us >= plan.end_us || silenced(tx_at_us))
            continue;
        uint8_t* chips = outgoing_chips_[armed.chip_buffer][static_cast<int>(payload)];
        const uint8_t tx_bytes = encode(payload_attempt, tx_at_us, armed.utc_s, chips);
        // INFO: fc 18oct26 no address, no burst | 13nov26 also B.5 holding a position
        if (tx_bytes == 0) continue;
        ports::RfPlan with_burst = plan;
        with_burst.tx = chips;
        with_burst.tx_bytes = tx_bytes;
        with_burst.tx_at_us = tx_at_us;
        if (context_.roles.rf.arm(with_burst) != Status::Ok) {
            if (!dwell_armed) {
                refuse_dwell(slot, first, phase, role, true, now_ms);
                return;
            }
            if (role == Role::Current) {
                context_.state.rf.timing_statistics.record_missed();
                log_refusal(radio::Verdict::Unarmed, slot, now_ms);
            }
            continue;
        }
        dwell_armed = true;
        carried_now = true;
        armed.carry(payload, tx_at_us);
        if (payload == Payload::Position) carry_position(payload_attempt, armed.utc_s);
        if (payload == Payload::Callsign) context_.state.rf.callsign_at_us = tx_at_us;
        if (payload == Payload::Status) context_.state.rf.status_at_us = tx_at_us;
    }
    if (!dwell_armed && context_.roles.rf.arm(plan) != Status::Ok) {
        refuse_dwell(slot, first, phase, role, false, now_ms);
        return;
    }
    if (rearmed && !carried_now) return;

    arm_count_++;
    if (role == Role::Next) {
        next_dwell_ = armed;
        next_armed_ = true;
    } else if (rearmed) {
        current_dwell_ = armed;
    } else {
        fly(armed);
    }
    record_dwell(slot, first, phase, /*armed=*/true, carried_now, now_ms);
}

// INFO: fc 27sep26 a refused next dwell is asked for again next pass, and reported once it flies
void RadioService::refuse_dwell(const timing::SlotPlan& slot,
                                const timing::Transmitter::Attempt& first, int phase, Role role,
                                bool carried_tx, uint32_t now_ms) {
    if (role == Role::Next) return;
    context_.state.rf.timing_statistics.record_missed();
    if (carried_tx) log_refusal(radio::Verdict::Unarmed, slot, now_ms);
    record_dwell(slot, first, phase, /*armed=*/false, /*carries_tx=*/false, now_ms);
}

void RadioService::collect_outcome(uint32_t now_ms) {
    const uint64_t now_us = pass_us_;
    const uint32_t tx_ok = context_.state.air.tx_ok;
    const bool reported = tx_ok != seen_tx_ok_;
    if (reported) held_logged_ = false;
    for (; seen_tx_ok_ != tx_ok; seen_tx_ok_++) {
        // INFO: fc 20nov26 a pass so slow that the queued dwell flew, burst and all, before it ran
        if (!current_dwell_.carries_any() && next_armed_ && next_dwell_.carries_any() &&
            now_us >= next_dwell_.from_us)
            promote();
        credit(context_.state.rf.tx_done_at_us(seen_tx_ok_, tx_ok), now_ms);
    }
    if (reported) context_.state.duty.tx_keyed_ms = transmitter_.air_time().total_ms();
    expire(current_dwell_, now_us);
    expire(retired_dwell_, now_us);
}

void RadioService::credit(uint64_t tx_at_us, uint32_t now_ms) {
    Armed* const dwells[] = {&current_dwell_, &retired_dwell_,
                             next_armed_ ? &next_dwell_ : nullptr};
    for (Armed* dwell : dwells) {
        if (dwell == nullptr) continue;
        for (const Payload payload : kEveryPayload) {
            if (dwell->carries(payload) && dwell->at(payload) == tx_at_us) {
                spend(*dwell, payload, now_ms);
                return;
            }
        }
    }
    // INFO: fc 27sep26 a TxDone nothing claims still spent air time, which the budget counts
    if (current_dwell_.carries_any())
        spend(current_dwell_, current_dwell_.first(), now_ms);
    else
        transmitter_.sent(current_dwell_.utc_s, now_ms, Payload::Position);
}

void RadioService::spend(Armed& dwell, Payload payload, uint32_t now_ms) {
    transmitter_.sent(dwell.utc_s, now_ms, payload);
    contention_[static_cast<int>(payload)].open = false;
    if (payload == Payload::Callsign) context_.state.air.tx_named++;
    dwell.drop(payload);
}

// INFO: fc 15sep26 a dwell that ended unreported took the radio with it, and is counted here
void RadioService::expire(Armed& dwell, uint64_t now_us) {
    const uint64_t max_age_us = static_cast<uint64_t>(kTxOutcomeMaxAgeMs) * 1000;
    if (dwell.carries_any() && now_us >= dwell.until_us + max_age_us) miss(dwell);
}

void RadioService::miss(Armed& dwell) {
    for (const Payload payload : kEveryPayload) {
        if (!dwell.carries(payload)) continue;
        context_.state.rf.timing_statistics.record_missed();
        dwell.drop(payload);
    }
}

}  // namespace skyblip::go
