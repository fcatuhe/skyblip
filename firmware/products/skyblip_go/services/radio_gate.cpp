#include "core/gnss/nmea.h"
#include "products/skyblip_go/services/radio.h"

namespace skyblip::go {

// INFO: fc 19nov26 every requirement in one place, before any burst of this pass is decided
void RadioService::judge_transmit(uint32_t now_ms) {
    const model::Ownship& own = context_.state.own;
    const timing::ClockState& clock = context_.state.clock;
    timing::Requirements met{};
    met.address = context_.state.activation.address().has_value();
    met.radio = radio_answers(now_ms);
    met.receiver = receiver_holds_rate();
    met.fix = own.fix_valid;
    met.settled = own.tx_settled;
    met.utc = own.utc_valid;
    met.pps = clock.pps_locked;
    context_.state.transmit.judge(met, now_ms);
    if (transmitted_ && !context_.state.transmit.transmits()) withdraw_carried();
    transmitted_ = context_.state.transmit.transmits();
}

// INFO: fc 19nov26 a closing gate takes back the bursts handed on, as silence_after() does
void RadioService::withdraw_carried() {
    Armed* const dwells[] = {&current_dwell_, next_armed_ ? &next_dwell_ : nullptr};
    for (Armed* dwell : dwells) {
        if (dwell == nullptr) continue;
        for (const Payload payload : kEveryPayload)
            if (dwell->carries(payload) &&
                context_.roles.rf.withdraw(dwell->at(payload)) == Status::Ok)
                dwell->drop(payload);
    }
}

// INFO: fc 19nov26 D.3: every dwell entered and the channel read it listens on, all recent
bool RadioService::radio_answers(uint32_t now_ms) {
    const bool asked = context_.state.activation.address().has_value();
    if (!asked || !dwell_started_ms_) dwell_started_ms_ = now_ms;
    if (!asked || !channel_read_ms_) channel_read_ms_ = now_ms;
    const std::optional<uint32_t> oldest = dwells_refused_ms_[kRadioRefusalSamples - 1];
    const bool refused = oldest && now_ms - *oldest <= kRadioAnswerMaxAgeMs;
    return !refused && now_ms - *dwell_started_ms_ <= kRadioAnswerMaxAgeMs &&
           now_ms - *channel_read_ms_ <= kRadioAnswerMaxAgeMs;
}

// INFO: fc 19nov26 G.1.1 and G.1.16: 2 Hz, each solution named .000 or .500 within 10 ms
bool RadioService::receiver_holds_rate() const {
    const model::Ownship& own = context_.state.own;
    const std::optional<int32_t> quarter_ms = gnss::named_quarter_ms(own.utc_ms);
    const bool on_half = quarter_ms && *quarter_ms % 500 == 0;
    return context_.roles.gnss.health().rate_held && on_half;
}

}  // namespace skyblip::go
