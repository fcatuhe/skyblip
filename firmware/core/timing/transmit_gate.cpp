#include "core/timing/transmit_gate.h"

namespace skyblip::timing {

void TransmitGate::judge(const Requirements& met, uint32_t now_ms) {
    met_ = met;
    if (met.fix && !first_fix_ms_) first_fix_ms_ = now_ms;
    unmet_ = first_unmet(met);
    transmits_ = unmet_ == NoTx::None && met.settled;
    if (unmet_ == NoTx::None) {
        unmet_since_ms_.reset();
        if (!met_since_ms_) met_since_ms_ = now_ms;
    } else {
        met_since_ms_.reset();
        if (!unmet_since_ms_) unmet_since_ms_ = now_ms;
    }
    shown_ = shown_now(now_ms);
}

// INFO: fc 19nov26 a mark stays until every requirement has held for kNoTxHoldMs
NoTx TransmitGate::shown_now(uint32_t now_ms) const {
    if (unmet_ == NoTx::None)
        return met_since_ms_ && now_ms - *met_since_ms_ >= kNoTxHoldMs ? NoTx::None : shown_;
    if (shown_ != NoTx::None) return unmet_;
    const bool held = unmet_since_ms_ && now_ms - *unmet_since_ms_ >= kNoTxHoldMs;
    return held && judged(now_ms) ? unmet_ : NoTx::None;
}

// INFO: fc 19nov26 an address and a radio need no sky, so they are judged from boot
bool TransmitGate::judged(uint32_t now_ms) const {
    if (unmet_ == NoTx::Address || unmet_ == NoTx::Radio) return true;
    return first_fix_ms_ && now_ms - *first_fix_ms_ >= kVerdictSettleMs;
}

NoTx first_unmet(const Requirements& met) {
    if (!met.address) return NoTx::Address;
    if (!met.radio) return NoTx::Radio;
    if (!met.receiver) return NoTx::Gnss;
    if (!met.fix) return NoTx::Fix;
    if (!met.utc) return NoTx::Utc;
    if (!met.pps) return NoTx::Pps;
    return NoTx::None;
}

}  // namespace skyblip::timing
