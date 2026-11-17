#ifndef SKYBLIP_CORE_INDICATION_SIGN_H
#define SKYBLIP_CORE_INDICATION_SIGN_H

#include <algorithm>
#include <cstdint>

#include "core/indication/lamp.h"
#include "core/power/shutdown.h"

namespace skyblip::indication {

// INFO: fc 13nov26 a pulse lights the lamp and runs the motor together, and a gap leaves both off
struct Sign {
    Colour lamp;
    uint8_t pulses;
    uint16_t pulse_ms;
    uint16_t gap_ms;
    uint16_t lead_ms;
};

// TODO: fc 13nov26 no hand has felt these through a motor that spins up, until #169 benches one
constexpr uint16_t kSignOnPulseMs = 400;
constexpr uint16_t kSignOffPulseMs = 150;
constexpr uint16_t kSignOffGapMs = 150;

inline constexpr Sign kSignOn{Colour::Green, 1, kSignOnPulseMs, 0, 0};
// INFO: fc 13nov26 the lead is a gap, so a traffic call cut by the shutdown never runs into pulse 1
inline constexpr Sign kSignOff{Colour::Red, 3, kSignOffPulseMs, kSignOffGapMs, kSignOffGapMs};

constexpr uint32_t length_ms(const Sign& sign) {
    return sign.lead_ms + static_cast<uint32_t>(sign.pulses) * sign.pulse_ms +
           static_cast<uint32_t>(sign.pulses - 1) * sign.gap_ms;
}

constexpr uint16_t shortest_phase_ms(const Sign& sign) {
    return sign.pulses > 1 ? std::min(sign.pulse_ms, sign.gap_ms) : sign.pulse_ms;
}

constexpr uint16_t kSignShortestPhaseMs =
    std::min(shortest_phase_ms(kSignOn), shortest_phase_ms(kSignOff));

static_assert(kSignShortestPhaseMs >= kShortestFlashEyeCanCatchMs, "a pulse a glance would miss");
static_assert(kSignOn.pulses != kSignOff.pulses, "on and off have to be told apart by the hand");
static_assert(length_ms(kSignOff) < power::kParkSettleMs,
              "the off sign ends inside the wait every shutdown already spends parking");

struct Signal {
    Colour lamp{Colour::None};
    bool vibrating{false};
};

class SignPlayer {
   public:
    void begin(const Sign& sign, uint32_t now_ms);
    void cancel() { sign_ = nullptr; }
    Signal update(uint32_t now_ms);

    bool playing() const { return sign_ != nullptr; }

   private:
    const Sign* sign_{nullptr};
    uint32_t started_ms_{0};
};

}  // namespace skyblip::indication

#endif
