#include "products/skyblip_go/services/radio.h"

#include <algorithm>

#include "core/model/ownship.h"

namespace skyblip::go {

Status RadioService::setup() {
    if (!ports::has(context_.roles.capabilities, ports::Capability::Rf)) return Status::Down;
    transmitter_.configure(context_.roles.device_addr);
    arm_dwell(timing::Scheduler::plan(phase_ms(), context_.state.clock), dwell_origin_us(), 0,
              Role::Flying);
    return Status::Ok;
}

// The next dwell is handed to the executor as soon as the current one flies, so
// the edge between them is the executor's to keep, to the microsecond, on its own
// thread. A pass of this loop only ever notices an edge after it went by.
void RadioService::tick(uint32_t now_ms) {
    accrue_armed();
    const timing::SlotPlan plan = timing::Scheduler::plan(phase_ms(), context_.state.clock);
    context_.state.rf.plan = plan;
    take_carrier_samples();
    take_switching();
    collect_outcome(now_ms);

    if (has_next_ && context_.roles.clock.micros() >= next_.from_us) promote();
    // INFO: fc 27sep26 200..205 is the one guard the map already names after the dwell it leads to
    if (has_next_ && holds(next_, plan)) {
        publish_dwell(now_ms);
        return;
    }
    const int64_t origin_us = dwell_origin_us();
    if (!holds(flying_, plan)) {
        if (flying_.mode != ports::RfMode::Idle) context_.roles.rf.abort();
        has_next_ = false;
        arm_dwell(plan, origin_us, now_ms, Role::Flying);
    } else if (transmit_due(plan, origin_us, now_ms)) {
        arm_dwell(plan, origin_us, now_ms, Role::Flying);
    }
    queue_next(plan, origin_us, now_ms);
    publish_dwell(now_ms);
}

// The one place the phase this service arms against leaves it. core/timing's
// durable-write policy needs the radio's own view of the second, not a second
// copy of the slot arithmetic, and it needs to know a burst is in flight - which
// only the arming code and the outcome collector between them can say. A burst
// queued on the next dwell is not in flight yet.
void RadioService::publish_dwell(uint32_t now_ms) {
    timing::DwellPhase& dwell = context_.state.rf.dwell;
    dwell.at_ms = now_ms;
    dwell.phase_ms = phase_ms();
    dwell.armed = flying_.mode != ports::RfMode::Idle;
    dwell.burst_armed = flying_.tx;
    context_.state.rf.noise_dbm = noise_.dbm();
    context_.state.rf.duty_permille = duty_permille(now_ms);
}

void RadioService::accrue_armed() {
    const uint64_t now_us = context_.roles.clock.micros();
    armed_us_ += armed_between(flying_, accounted_us_, now_us);
    if (has_next_) armed_us_ += armed_between(next_, accounted_us_, now_us);
    accounted_us_ = now_us;
    context_.state.duty.rx_armed_ms = static_cast<uint32_t>(armed_us_ / 1000);
}

uint64_t RadioService::armed_between(const Armed& dwell, uint64_t from_us, uint64_t to_us) {
    return std::clamp(to_us, dwell.from_us, dwell.until_us) -
           std::clamp(from_us, dwell.from_us, dwell.until_us);
}

// From the latched edge, at the instant it is asked for. Deriving it from a
// phase the board sampled at the top of the pass costs however long the pass
// takes to reach this service, which is the whole jitter guard on a bad pass.
//
// Both branches read micros(), which is 64-bit and does not wrap. The free-running
// fallback used to be now_ms % 1000, and that is not a phase: 2^32 ms is not a
// whole number of seconds, so at the 49.7-day wrap of ports::Clock::millis() the
// second stepped 705 ms BACKWARDS and one dwell was armed out of order. It only
// showed with the anchor already lost, which is the worst time to add a fault.
int RadioService::phase_ms() const {
    const timing::ClockState& clock = context_.state.clock;
    const uint64_t now_us = context_.roles.clock.micros();
    if (clock.pps_locked && now_us >= clock.pps_edge_us)
        return static_cast<int>((now_us - clock.pps_edge_us) / 1000 % 1000);
    return static_cast<int>(now_us / 1000 % 1000);
}

void RadioService::take_switching() {
    const ports::RfSwitching s = context_.roles.rf.switching();
    context_.state.rf.timing_stats.record_switching(s.hop_us, s.to_oband_us, s.to_mband_us, s.late);
}

void RadioService::take_carrier_samples() {
    const ports::RfCarrier carrier = context_.roles.rf.carrier();
    if (carrier.samples == seen_carrier_samples_) return;
    seen_carrier_samples_ = carrier.samples;
    noise_.sample(carrier.dbm);
}

// The top of the second phase_ms() counts from, to the microsecond: the latched
// edge a whole number of seconds back, never the phase rounded to a millisecond.
int64_t RadioService::second_origin_us() const {
    const timing::ClockState& clock = context_.state.clock;
    const uint64_t now_us = context_.roles.clock.micros();
    if (clock.pps_locked && now_us >= clock.pps_edge_us)
        return static_cast<int64_t>(clock.pps_edge_us +
                                    (now_us - clock.pps_edge_us) / kSecondUs * kSecondUs);
    return static_cast<int64_t>(now_us - now_us % kSecondUs);
}

// Signed, because the tail of slot 1 in the first second after boot belongs to
// a second that started before the clock did.
int64_t RadioService::dwell_origin_us() const {
    const int64_t origin_us = second_origin_us();
    const bool in_slot1_tail = phase_ms() < timing::kSlot1Wrap;
    return in_slot1_tail ? origin_us - static_cast<int64_t>(kSecondUs) : origin_us;
}

uint64_t RadioService::at_us(int64_t origin_us, int phase_ms) {
    return static_cast<uint64_t>(std::max<int64_t>(0, origin_us + int64_t{phase_ms} * 1000));
}

RadioService::Upcoming RadioService::upcoming_after(const timing::SlotPlan& plan,
                                                    int64_t origin_us) const {
    const timing::ClockState& clock = context_.state.clock;
    if (plan.start_ms == timing::kSlot0Start)
        return {timing::Scheduler::plan(timing::kSlot1Start, clock), origin_us};
    if (plan.start_ms == timing::kSlot1Start)
        return {timing::Scheduler::plan(timing::kUplinkRxStart, clock),
                origin_us + static_cast<int64_t>(kSecondUs)};
    return {timing::Scheduler::plan(timing::kSlot0Start, clock), origin_us};
}

bool RadioService::holds(const Armed& dwell, const timing::SlotPlan& plan) {
    return dwell.mode == mode_for(plan) && dwell.freq_hz == plan.freq_hz;
}

uint64_t RadioService::instant_us(const timing::Transmitter::Attempt& attempt, int64_t origin_us) {
    return at_us(origin_us, attempt.at_ms);
}

void RadioService::promote() {
    flying_ = next_;
    has_next_ = false;
    if (!flying_.tx) return;
    context_.state.rf.tx_deadline_us = flying_.tx_at_us;
    context_.state.rf.tx_callsign = flying_.payload == timing::Transmitter::Payload::Callsign;
}

// Two clocks meet here. The TimeStamp field counts quarter seconds from the top
// of the UTC second the slot belongs to, which the latched PPS edge anchors;
// the position has to be carried over the interval since the fix, which is a
// monotonic one. The dwell is armed before the slot opens, so the second figure
// is the staleness already accrued plus the whole wait still to come.
protocol::BurstInstant RadioService::burst_instant(const timing::Transmitter::Attempt& a,
                                                   uint64_t tx_at_us, uint32_t utc) const {
    const uint32_t tx_ms = static_cast<uint32_t>(tx_at_us / 1000);
    protocol::BurstInstant at{};
    at.utc = utc;
    at.into_utc_ms = a.at_ms;
    at.since_fix_ms = static_cast<int32_t>(tx_ms - context_.state.own.fix_ms);
    return at;
}

uint32_t RadioService::slot_utc(uint32_t now_ms) const {
    const uint32_t utc = context_.state.traffic_now(now_ms);
    const bool in_slot1_tail = phase_ms() < timing::kSlot1Wrap;
    return (in_slot1_tail && utc > 0) ? utc - 1 : utc;
}

ports::RfMode RadioService::mode_for(const timing::SlotPlan& plan) {
    return plan.band == timing::Band::O ? ports::RfMode::RxOband : ports::RfMode::RxMband;
}

// The M band carries two systems past one sync window. The O band carries the
// ground station's uplink and nothing else. Both halves of a dwell are set
// here: what to listen for, and the modulation to listen with - §C.4 runs at
// twice §C.2's chip rate through a Gaussian filter and a wider receiver, so a
// dwell that only moved the synthesiser was tuned to the O band and deaf on it.
void RadioService::listen_for(timing::Band band, ports::RfPlan& plan) {
    if (band == timing::Band::O) {
        plan.sync = protocol::kUplinkSync;
        plan.sync_bits = protocol::kUplinkSyncBits;
        plan.rx_len = protocol::kUplinkFrameBytes;
        plan.bitrate = protocol::kUplinkChipRateBps;
        plan.fdev_hz = protocol::kUplinkDeviationHz;
        plan.bandwidth_hz = protocol::kUplinkChannelBandwidthHz;
        plan.gaussian_bt_e2 = protocol::kUplinkGaussianBtE2;
        return;
    }
    plan.sync = protocol::kSharedSync;
    plan.sync_bits = protocol::kSharedSyncBits;
    plan.rx_len = protocol::kRxChipBytes;
    plan.bitrate = protocol::kMbandChipRateBps;
    plan.fdev_hz = protocol::kMbandDeviationHz;
    plan.bandwidth_hz = protocol::kMbandChannelBandwidthHz;
    plan.gaussian_bt_e2 = protocol::kMbandGaussianBtE2;
}

// INFO: fc 15sep26 the gates a burst waits on clear inside the dwell, ports::Rf::arm() joins it
// there
bool RadioService::transmit_due(const timing::SlotPlan& plan, int64_t origin_us,
                                uint32_t now_ms) const {
    if (flying_.tx) return false;
    const timing::Transmitter::Attempt a = attempt(plan, now_ms);
    return a.go && instant_us(a, origin_us) > context_.roles.clock.micros();
}

// The next dwell is queued bare when its burst is not decided yet, and queued
// again carrying it the pass its gates clear, which is still ahead of its start.
void RadioService::queue_next(const timing::SlotPlan& plan, int64_t origin_us, uint32_t now_ms) {
    if (flying_.mode == ports::RfMode::Idle) return;
    const Upcoming next = upcoming_after(plan, origin_us);
    if (has_next_ && (next_.tx || !attempt(next.plan, now_ms).go)) return;
    arm_dwell(next.plan, next.origin_us, now_ms, Role::Next);
}

timing::Transmitter::Attempt RadioService::attempt(const timing::SlotPlan& plan,
                                                   uint32_t now_ms) const {
    const model::OwnState& own = context_.state.own;
    // F5: a cold receiver's first solutions walk, and the flight state derived
    // from them decides our transmit rate. Nothing goes on air until own-ship
    // says the solution behind it has settled.
    if (!timing::own_ship_transmits(own, context_.state.clock))
        return timing::Transmitter::Attempt{};
    const bool full_rate =
        !flight::reduced_rate(flight::announced_state(own.flight_state, own.aircraft_cat));
    const timing::Transmitter::Attempt a =
        transmitter_.attempt(plan, slot_utc(now_ms), now_ms, full_rate, fix_lag_ms());
    if (a.payload == timing::Transmitter::Payload::Callsign && settings_.callsign[0] == 0)
        return timing::Transmitter::Attempt{};
    return a;
}

// INFO: fc 13sep26 zero when this second's solution is in hand, a whole second when one was missed
int32_t RadioService::fix_lag_ms() const {
    const uint32_t second_ms = static_cast<uint32_t>(at_us(dwell_origin_us(), 0) / 1000);
    return static_cast<int32_t>(second_ms - context_.state.own.fix_ms);
}

void RadioService::arm_dwell(const timing::SlotPlan& slot, int64_t origin_us, uint32_t now_ms,
                             Role role) {
    const int phase = phase_ms();
    const uint64_t now_us = context_.roles.clock.micros();

    ports::RfPlan plan{};
    plan.mode = mode_for(slot);
    plan.freq_hz = slot.freq_hz;
    // The one setting on this device that is a property of the unit's own
    // reference rather than of the pilot: it belongs to every dwell, on both
    // bands, receiving as well as transmitting (core/settings/settings.h).
    plan.freq_corr_e1_ppm = settings_.freq_trim_e1_ppm;
    listen_for(slot.band, plan);
    plan.end_us = at_us(origin_us, slot.end_ms);
    if (plan.end_us <= now_us) return;
    // Arming inside the window means the dwell has already started: begin now and
    // keep the same hard stop, rather than waiting a whole second for the next one.
    const uint64_t opens_us = at_us(origin_us, slot.start_ms);
    plan.start_us = role == Role::Next ? opens_us : std::max(opens_us, now_us);

    const timing::Transmitter::Attempt a = attempt(slot, now_ms);
    over_budget_ = a.over_budget;
    // The one place this policy's own refusal is decided: a plan the hour's
    // air-time budget already refused to arm, counted apart from a dwell that
    // was armed and then missed its outcome.
    if (a.over_budget) {
        context_.state.rf.timing_stats.record_refused();
        if (!held_logged_) {
            log_refusal(radio::Event::Held, slot, now_ms);
            held_logged_ = true;
        }
    }
    const uint64_t tx_at_us = instant_us(a, origin_us);
    const bool carries_tx =
        a.go && tx_at_us >= std::max(plan.start_us, now_us) && tx_at_us < plan.end_us;
    const uint8_t buffer =
        role == Role::Next ? static_cast<uint8_t>(flying_.buffer ^ 1u) : flying_.buffer;
    const uint32_t utc = slot_utc(now_ms);
    if (carries_tx) {
        if (a.payload == timing::Transmitter::Payload::Callsign)
            protocol::from_own_callsign(outgoing_, context_.roles.device_addr,
                                        settings::kAddrTableSkyblip, settings_.callsign);
        else
            protocol::from_own(outgoing_, context_.state.own, context_.roles.device_addr,
                               settings::kAddrTableSkyblip, context_.state.own.aircraft_cat,
                               burst_instant(a, tx_at_us, utc));
        outgoing_.scramble();
        outgoing_.set_crc();
        plan.tx = outgoing_chips_[buffer];
        plan.tx_len = static_cast<uint8_t>(
            protocol::mband_payload(protocol::kAdslSyncWord, outgoing_.Data,
                                    protocol::kAdslFrameBytes, outgoing_chips_[buffer]));
        plan.tx_at_us = tx_at_us;
    }

    if (context_.roles.rf.arm(plan) != Status::Ok) {
        // The next dwell is asked for again on the next pass, and anything it
        // loses by then is reported as the flying dwell it will have become.
        if (role == Role::Next) return;
        // A dwell refused before it could even start: ports::Rf's own "a plan
        // that cannot complete before its end is refused here rather than
        // truncated on air", read out on the bench.
        context_.state.rf.timing_stats.record_missed();
        if (carries_tx) log_refusal(radio::Event::Unarmed, slot, now_ms);
        record_dwell(slot, a, phase, /*armed=*/false, /*carries_tx=*/false, now_ms);
        return;
    }
    arm_count_++;
    Armed armed{};
    armed.mode = plan.mode;
    armed.freq_hz = plan.freq_hz;
    armed.from_us = plan.start_us;
    armed.until_us = plan.end_us;
    armed.tx = carries_tx;
    armed.payload = a.payload;
    armed.utc = utc;
    armed.tx_at_us = tx_at_us;
    armed.buffer = buffer;
    if (role == Role::Next) {
        next_ = armed;
        has_next_ = true;
    } else {
        flying_ = armed;
        if (carries_tx) {
            context_.state.rf.tx_deadline_us = tx_at_us;
            context_.state.rf.tx_callsign = a.payload == timing::Transmitter::Payload::Callsign;
        }
    }
    record_dwell(slot, a, phase, /*armed=*/true, carries_tx, now_ms);
}

void RadioService::record_dwell(const timing::SlotPlan& slot,
                                const timing::Transmitter::Attempt& attempt, int phase, bool armed,
                                bool carries_tx, uint32_t now_ms) {
    if (!context_.diag.armed()) return;
    diag::Dwell value{};
    value.freq_hz = slot.freq_hz;
    value.start_ms = static_cast<uint16_t>(slot.start_ms);
    value.end_ms = static_cast<uint16_t>(slot.end_ms);
    value.phase_ms = static_cast<uint16_t>(phase);
    value.duty_permille = static_cast<uint16_t>(duty_permille(now_ms));
    value.state = slot.state;
    value.band = slot.band;
    value.refusal = refusal_of(slot, attempt, armed, carries_tx);
    value.noise_dbm = noise_.dbm();
    value.tx_allowed = slot.tx_allowed;
    value.own_tx_dwell = slot.own_tx_dwell;
    value.listen_only = slot.listen_only;
    value.armed = armed;
    value.burst_armed = carries_tx;
    context_.diag.record(value, context_.instant(now_ms));
}

// INFO: fc 20sep26 a dwell the slot map never offered own-ship refuses nothing, so it names nothing
diag::Refusal RadioService::refusal_of(const timing::SlotPlan& slot,
                                       const timing::Transmitter::Attempt& attempt, bool armed,
                                       bool carries_tx) const {
    if (!slot.tx_allowed) return diag::Refusal::None;
    if (!armed) return diag::Refusal::Unarmed;
    if (carries_tx) return diag::Refusal::None;
    if (attempt.over_budget) return diag::Refusal::OverBudget;
    if (!timing::own_ship_transmits(context_.state.own, context_.state.clock))
        return diag::Refusal::Unsettled;
    return diag::Refusal::OffSchedule;
}

void RadioService::log_refusal(radio::Event outcome, const timing::SlotPlan& slot,
                               uint32_t now_ms) {
    const bus::State& state = context_.state;
    const events::Stamp stamp =
        events::stamp_of(context_.roles.clock.micros(), state.clock.pps_edge_us,
                         state.clock.pps_locked, state.traffic_now(now_ms));
    radio::Entry entry{};
    entry.event = outcome;
    entry.band = slot.band;
    entry.channel = slot.freq_hz == timing::kMband1Hz ? 1 : 0;
    entry.at_s = stamp.at_s;
    entry.into_ms = stamp.into_ms;
    entry.phase_valid = stamp.phase_valid;
    entry.utc = context_.instant(now_ms).utc_dated;
    entry.airborne = flight::airborne(state.own.flight_state);
    context_.state.radio_log.record(entry);
    context_.diag.record(entry);
}

void RadioService::collect_outcome(uint32_t now_ms) {
    const uint64_t now_us = context_.roles.clock.micros();
    if (context_.state.air.tx_ok != seen_tx_ok_) {
        seen_tx_ok_ = context_.state.air.tx_ok;
        held_logged_ = false;
        // A pass slow enough that the queued dwell flew, and its burst went out, before this one
        // ran.
        if (!flying_.tx && has_next_ && next_.tx && now_us >= next_.from_us) promote();
        transmitter_.sent(flying_.utc, now_ms, flying_.payload);
        context_.state.duty.tx_keyed_ms = transmitter_.air_time().total_ms();
        if (flying_.payload == timing::Transmitter::Payload::Callsign)
            context_.state.air.tx_named++;
        // The executor's own report against the deadline this dwell was armed
        // for: both absolute instants on the same clock, so slot 1's wrap
        // costs this nothing.
        context_.state.rf.timing_stats.record_dwell_phase(
            static_cast<int64_t>(context_.state.air.last_tx_done_at_us) -
            static_cast<int64_t>(context_.state.rf.tx_deadline_us));
        flying_.tx = false;
    }
    // INFO: fc 15sep26 a dwell that ended unreported took the radio with it, and is counted here
    if (flying_.tx && now_us >= flying_.until_us) {
        context_.state.rf.timing_stats.record_missed();
        flying_.tx = false;
    }
}

}  // namespace skyblip::go
