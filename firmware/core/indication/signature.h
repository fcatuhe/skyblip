#ifndef SKYBLIP_CORE_INDICATION_SIGNATURE_H
#define SKYBLIP_CORE_INDICATION_SIGNATURE_H

#include <algorithm>
#include <cstdint>

#include "core/indication/lamp.h"
#include "core/power/shutdown.h"

namespace skyblip::indication {

// INFO: fc 13nov26 a pulse lights the lamp and runs the motor together, and a gap leaves both off
struct Signature {
    indication::Lamp lamp;
    uint8_t pulses;
    uint16_t pulse_ms;
    uint16_t gap_ms;
};

// TODO: fc 13nov26 no hand has felt these through a motor that spins up, until #169 benches one
constexpr uint16_t kSignOnPulseMs = 400;
constexpr uint16_t kSignOffPulseMs = 150;
constexpr uint16_t kSignOffGapMs = 150;

inline constexpr Signature kSignOn{indication::Lamp::Green, 1, kSignOnPulseMs, 0};
inline constexpr Signature kSignOff{indication::Lamp::Red, 3, kSignOffPulseMs, kSignOffGapMs};

constexpr uint32_t length_ms(const Signature& signature) {
    return static_cast<uint32_t>(signature.pulses) * signature.pulse_ms +
           static_cast<uint32_t>(signature.pulses - 1) * signature.gap_ms;
}

constexpr uint16_t shortest_phase_ms(const Signature& signature) {
    return signature.pulses > 1 ? std::min(signature.pulse_ms, signature.gap_ms)
                                : signature.pulse_ms;
}

constexpr uint16_t kSignatureShortestPhaseMs =
    std::min(shortest_phase_ms(kSignOn), shortest_phase_ms(kSignOff));

static_assert(kSignatureShortestPhaseMs >= kShortestFlashEyeCanCatchMs,
              "a pulse a glance would miss");
static_assert(kSignOn.pulses != kSignOff.pulses, "on and off have to be told apart by the hand");
static_assert(length_ms(kSignOff) < power::kParkSettleMs,
              "the off signature ends inside the wait every shutdown already spends parking");

struct Signal {
    indication::Lamp lamp{indication::Lamp::None};
    bool vibrating{false};
};

class SignaturePlayer {
   public:
    void begin(const Signature& signature, uint32_t now_ms);
    void cancel() { signature_ = nullptr; }
    Signal update(uint32_t now_ms);

    bool playing() const { return signature_ != nullptr; }

   private:
    const Signature* signature_{nullptr};
    uint32_t started_ms_{0};
};

}  // namespace skyblip::indication

#endif
