#include "products/skyblip_go/services/radio.h"

namespace skyblip::go {

// INFO: fc 14nov26 the executor drops a busy burst unkeyed, and ADS-L §D.3 decides what follows
void RadioService::collect_busy(uint32_t now_ms) {
    const bus::RfState& rf = context_.state.rf;
    for (; seen_busy_ != rf.busy_reports; seen_busy_++) {
        const bus::RfState::Busy* busy = rf.busy_at(seen_busy_);
        if (busy == nullptr) continue;
        Payload payload = Payload::Position;
        Armed* dwell = carrying(busy->tx_at_us, payload);
        if (dwell != nullptr) contend(*dwell, payload, *busy, now_ms);
    }
}

RadioService::Armed* RadioService::carrying(uint64_t tx_at_us, Payload& payload) {
    Armed* const dwells[] = {&current_dwell_, &retired_dwell_,
                             next_armed_ ? &next_dwell_ : nullptr};
    for (Armed* dwell : dwells) {
        if (dwell == nullptr) continue;
        for (const Payload carried : kEveryPayload) {
            if (dwell->carries(carried) && dwell->at(carried) == tx_at_us) {
                payload = carried;
                return dwell;
            }
        }
    }
    return nullptr;
}

// INFO: fc 14nov26 nothing is forced airborne, where a 2 s silence would cost two positions
void RadioService::contend(Armed& dwell, Payload payload, const bus::RfState::Busy& busy,
                           uint32_t now_ms) {
    const uint64_t busy_at_us = busy.tx_at_us;
    dwell.drop(payload);
    const bool airborne = full_rate();
    Contention& contention = contention_[static_cast<int>(payload)];
    const uint32_t turn_s = transmitter_.turn_of(payload, dwell.utc_s, airborne);
    if (!contention.open || contention.turn_s != turn_s)
        contention = Contention{true, turn_s, busy_at_us};

    const int slot = dwell.frequency_hz == adsl::kMBand1Hz ? 1 : 0;
    adsl::ChannelAttempt attempt{};
    attempt.at_us = busy_at_us;
    attempt.first_us = contention.first_us;
    if (forced_us_) attempt.forced_end_us = *forced_us_ + kAirTimeUs;
    attempt.earliest_us = context_.roles.clock.microseconds() + timing::PoliteAccess::kCcaLeadUs;
    attempt.last_us = at_us(dwell.origin_us, timing::Transmitter::last_instant(payload, slot));
    attempt.may_force = !airborne;
    const adsl::Deferral deferral{timing::PoliteAccess::kDeferralUnitUs,
                                  timing::PoliteAccess::kDeferralUnits,
                                  transmitter_.deferral_draw(busy_at_us)};
    const adsl::AccessVerdict verdict = adsl::media_access(attempt, deferral);

    bus::State::AirCounts& air = context_.state.air;
    const bool forced = verdict.access == adsl::Access::Force;
    const bool rearmed = (forced || verdict.access == adsl::Access::Defer) &&
                         rearm(dwell, payload, verdict.at_us, forced);
    if (!rearmed) {
        air.tx_busy_missed++;
        log_access(radio::Verdict::Busy, dwell, payload, busy, now_ms);
    } else if (forced) {
        air.tx_forced++;
        log_access(radio::Verdict::Forced, dwell, payload, busy, now_ms);
        forced_us_ = verdict.at_us;
        silence_after(verdict.at_us);
    } else {
        air.tx_deferred++;
        log_access(radio::Verdict::Deferred, dwell, payload, busy, now_ms);
    }
}

// INFO: fc 14nov26 G.1.1: a position is encoded again for its new instant, from the newest fix
bool RadioService::rearm(Armed& dwell, Payload payload, uint64_t tx_at_us, bool forced) {
    if (!timing::ownship_transmits(context_.state.own, context_.state.clock,
                                   context_.state.activation.address()))
        return false;
    if (silenced(tx_at_us)) return false;

    timing::Transmitter::Attempt attempt{};
    attempt.go = true;
    attempt.payload = payload;
    attempt.frequency_hz = dwell.frequency_hz;
    attempt.at_ms = static_cast<int>((static_cast<int64_t>(tx_at_us) - dwell.origin_us) / 1000);
    uint8_t* chips = outgoing_chips_[dwell.chip_buffer][static_cast<int>(payload)];
    const uint8_t tx_bytes = encode(attempt, tx_at_us, dwell.utc_s, chips);
    if (tx_bytes == 0) return false;

    ports::RfPlan plan{};
    plan.mode = dwell.mode;
    plan.frequency_hz = dwell.frequency_hz;
    plan.frequency_correction_e1_ppm = settings_.frequency_trim_e1_ppm;
    listen_for(timing::Band::M, plan);
    plan.start_us = dwell.from_us;
    plan.end_us = dwell.until_us;
    plan.tx = chips;
    plan.tx_bytes = tx_bytes;
    plan.tx_at_us = tx_at_us;
    plan.tx_forced = forced;
    if (context_.roles.rf.arm(plan) != Status::Ok) return false;
    dwell.carry(payload, tx_at_us);
    if (payload == Payload::Callsign) context_.state.rf.callsign_at_us = tx_at_us;
    if (payload == Payload::Status) context_.state.rf.status_at_us = tx_at_us;
    return true;
}

// INFO: fc 14nov26 dated at the burst's planned instant, with the level its assessment read
void RadioService::log_access(radio::Verdict verdict, const Armed& dwell, Payload payload,
                              const bus::RfState::Busy& busy, uint32_t now_ms) {
    const bus::State& state = context_.state;
    const events::Stamp stamp =
        events::stamp_of(busy.tx_at_us, state.clock.pps_edge_us, state.clock.pps_locked,
                         state.traffic_now_s(now_ms));
    radio::Entry entry{};
    entry.verdict = verdict;
    entry.band = timing::Band::M;
    entry.channel = dwell.frequency_hz == adsl::kMBand1Hz ? 1 : 0;
    entry.at_s = stamp.at_s;
    entry.into_ms = stamp.into_ms;
    entry.phase_valid = stamp.phase_valid;
    entry.utc_dated = context_.instant(now_ms).utc_dated;
    entry.airborne = flight::airborne(state.own.flight_state);
    entry.rssi_dbm = busy.level_dbm;
    entry.rssi_valid = busy.level_valid;
    entry.callsign = payload == Payload::Callsign;
    entry.status = payload == Payload::Status;
    context_.state.radio_log.record(entry);
    context_.diagnostics.record(entry);
}

bool RadioService::silenced(uint64_t tx_at_us) const {
    return forced_us_ && tx_at_us > *forced_us_ &&
           tx_at_us < *forced_us_ + kAirTimeUs + adsl::kSilenceAfterForceUs;
}

// INFO: fc 14nov26 §D.3's silence after a force reaches bursts already handed to the executor
void RadioService::silence_after(uint64_t forced_at_us) {
    Armed* const dwells[] = {&current_dwell_, next_armed_ ? &next_dwell_ : nullptr};
    for (Armed* dwell : dwells) {
        if (dwell == nullptr) continue;
        for (const Payload payload : kEveryPayload) {
            if (!dwell->carries(payload) || dwell->at(payload) == forced_at_us ||
                !silenced(dwell->at(payload)))
                continue;
            if (context_.roles.rf.withdraw(dwell->at(payload)) == Status::Ok) dwell->drop(payload);
        }
    }
}

}  // namespace skyblip::go
