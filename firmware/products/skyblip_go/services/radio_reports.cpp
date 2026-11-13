#include "products/skyblip_go/services/radio.h"

namespace skyblip::go {

void RadioService::take_switching(uint32_t now_ms) {
    const ports::RfSwitching switching = context_.roles.rf.switching();
    context_.state.rf.timing_statistics.record_switching(
        switching.hop_us, switching.to_o_band_us, switching.to_m_band_us, switching.late_starts);
    if (switching.switches == seen_switches_) return;
    seen_switches_ = switching.switches;
    record_switch(switching.last, now_ms);
}

void RadioService::record_switch(const ports::RfSwitch& change, uint32_t now_ms) {
    if (!context_.diagnostics.armed()) return;
    diagnostics::Switch value{};
    value.to_hz = change.to_hz;
    value.margin_us = static_cast<int32_t>(
        std::clamp<int64_t>(change.margin_us(), std::numeric_limits<int32_t>::min(),
                            std::numeric_limits<int32_t>::max()));
    value.took_us = clamp_uint16(change.took_us());
    value.gap_us = clamp_uint16(change.gap_us());
    value.change = kind_of(change);
    value.armed_ahead = change.armed_ahead;
    value.late = change.late_start();
    const timing::ClockState& clock = context_.state.clock;
    const events::Stamp stamp = events::stamp_of(
        change.ready_us, clock.pps_edge_us, clock.pps_locked, context_.state.traffic_now_s(now_ms));
    context_.diagnostics.record(value,
                                diagnostics::instant_of(stamp, context_.instant(now_ms).utc_dated));
}

diagnostics::SwitchKind RadioService::kind_of(const ports::RfSwitch& change) {
    if (change.from == ports::RfMode::Idle) return diagnostics::SwitchKind::Wake;
    if (change.from != change.to)
        return change.to == ports::RfMode::RxOBand ? diagnostics::SwitchKind::ToOBand
                                                   : diagnostics::SwitchKind::ToMBand;
    return change.from_hz != change.to_hz ? diagnostics::SwitchKind::Hop
                                          : diagnostics::SwitchKind::Retune;
}

void RadioService::take_carrier_samples() {
    const ports::RfCarrier carrier = context_.roles.rf.carrier();
    if (carrier.samples == seen_carrier_samples_) return;
    seen_carrier_samples_ = carrier.samples;
    noise_floor_.sample(carrier.level_dbm);
}

void RadioService::record_dwell(const timing::SlotPlan& slot,
                                const timing::Transmitter::Attempt& attempt, int phase, bool armed,
                                bool carries_tx, uint32_t now_ms) {
    if (!context_.diagnostics.armed()) return;
    diagnostics::Dwell value{};
    value.frequency_hz = slot.frequency_hz;
    value.start_ms = static_cast<uint16_t>(slot.start_ms);
    value.end_ms = static_cast<uint16_t>(slot.end_ms);
    value.phase_ms = static_cast<uint16_t>(phase);
    value.duty_permille = static_cast<uint16_t>(duty_permille(now_ms));
    value.state = slot.state;
    value.band = slot.band;
    value.refusal = refusal_of(slot, attempt, armed, carries_tx);
    value.noise_dbm = noise_floor_.level_dbm();
    value.tx_allowed = slot.tx_allowed;
    value.own_tx_dwell = slot.own_tx_dwell;
    value.listen_only = slot.listen_only;
    value.armed = armed;
    value.burst_armed = carries_tx;
    context_.diagnostics.record(value, context_.instant(now_ms));
}

// INFO: fc 20sep26 a dwell the slot map never offered own-ship refuses nothing, so it names nothing
diagnostics::Refusal RadioService::refusal_of(const timing::SlotPlan& slot,
                                              const timing::Transmitter::Attempt& attempt,
                                              bool armed, bool carries_tx) const {
    if (!slot.tx_allowed) return diagnostics::Refusal::None;
    if (!armed) return diagnostics::Refusal::Unarmed;
    if (carries_tx) return diagnostics::Refusal::None;
    if (attempt.over_budget) return diagnostics::Refusal::OverBudget;
    if (!timing::ownship_transmits(context_.state.own, context_.state.clock,
                                   context_.state.activation.address()))
        return diagnostics::Refusal::Unsettled;
    return diagnostics::Refusal::OffSchedule;
}

void RadioService::log_refusal(radio::Verdict outcome, const timing::SlotPlan& slot,
                               uint32_t now_ms) {
    const bus::State& state = context_.state;
    const events::Stamp stamp =
        events::stamp_of(context_.roles.clock.microseconds(), state.clock.pps_edge_us,
                         state.clock.pps_locked, state.traffic_now_s(now_ms));
    radio::Entry entry{};
    entry.verdict = outcome;
    entry.band = slot.band;
    entry.channel = slot.frequency_hz == adsl::kMBand1Hz ? 1 : 0;
    entry.at_s = stamp.at_s;
    entry.into_ms = stamp.into_ms;
    entry.phase_valid = stamp.phase_valid;
    entry.utc_dated = context_.instant(now_ms).utc_dated;
    entry.airborne = flight::airborne(state.own.flight_state);
    context_.state.radio_log.record(entry);
    context_.diagnostics.record(entry);
}

}  // namespace skyblip::go
