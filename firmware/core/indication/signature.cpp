#include "core/indication/signature.h"

namespace skyblip::indication {

void SignaturePlayer::begin(const Signature& signature, uint32_t now_ms) {
    signature_ = &signature;
    started_ms_ = now_ms;
}

Signal SignaturePlayer::update(uint32_t now_ms) {
    if (!signature_) return Signal{};
    const uint32_t elapsed = now_ms - started_ms_;
    if (elapsed >= length_ms(*signature_)) {
        signature_ = nullptr;
        return Signal{};
    }
    if (elapsed < signature_->lead_ms) return Signal{};
    const uint32_t period = static_cast<uint32_t>(signature_->pulse_ms) + signature_->gap_ms;
    const bool pulsing = (elapsed - signature_->lead_ms) % period < signature_->pulse_ms;
    return Signal{pulsing ? signature_->lamp : indication::Lamp::None, pulsing};
}

}  // namespace skyblip::indication
