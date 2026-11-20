#include "core/timing/transmit_gate.h"

namespace skyblip::timing {

NoTx first_unmet(const Requirements& met) {
    if (!met.address) return NoTx::Address;
    if (!met.radio) return NoTx::Radio;
    if (!met.receiver) return NoTx::Gnss;
    if (!met.fix) return NoTx::Fix;
    if (!met.utc) return NoTx::Utc;
    if (!met.pps) return NoTx::Pps;
    return NoTx::None;
}

void TransmitGate::judge(const Requirements& met, uint32_t now_ms) {
    met_ = met;
    if (met.fix && !first_fix_ms_) first_fix_ms_ = now_ms;
    unmet_ = first_unmet(met);
    transmits_ = unmet_ == NoTx::None && met.settled;
    if (unmet_ == NoTx::None) {
        unmet_since_ms_.reset();
    } else if (!unmet_since_ms_) {
        unmet_since_ms_ = now_ms;
    }
    const bool held = unmet_since_ms_ && now_ms - *unmet_since_ms_ >= kNoTxHoldMs;
    shown_ = held && judged(now_ms) ? unmet_ : NoTx::None;
}

// INFO: fc 19nov26 an address and a radio need no sky, so they are judged from boot
bool TransmitGate::judged(uint32_t now_ms) const {
    if (unmet_ == NoTx::Address || unmet_ == NoTx::Radio) return true;
    return first_fix_ms_ && now_ms - *first_fix_ms_ >= kVerdictSettleMs;
}

}  // namespace skyblip::timing
