#ifndef SKYBLIP_RUNTIME_LOOP_METER_H
#define SKYBLIP_RUNTIME_LOOP_METER_H

#include <cstdint>

#include "core/diag/payload.h"
#include "ports/clock.h"

namespace skyblip::runtime {

// INFO: fc 03oct26 one window a second: how long the loop worked, and how long nothing ran it
class LoopMeter {
   public:
    explicit LoopMeter(const ports::Clock& clock) : clock_(clock) {}

    void begin_pass(uint16_t phase_ms) {
        const uint64_t now_us = clock_.micros();
        if (began_) {
            const uint64_t gap_ms = (now_us - pass_began_us_) / 1000;
            if (gap_ms > window_.worst_gap_ms) {
                window_.worst_gap_ms = gap_ms > 0xFFFF ? 0xFFFF : static_cast<uint16_t>(gap_ms);
                window_.worst_gap_phase_ms = phase_ms;
            }
        }
        began_ = true;
        pass_began_us_ = now_us;
        pass_phase_ms_ = phase_ms;
    }

    template <class Work>
    void timed(uint8_t who, Work&& work) {
        const uint64_t began_us = clock_.micros();
        work();
        const uint64_t took_us = clock_.micros() - began_us;
        if (took_us > worst_tick_us_) {
            worst_tick_us_ = took_us;
            window_.worst_service = who;
        }
    }

    void end_pass() {
        const uint64_t took_us = clock_.micros() - pass_began_us_;
        window_.passes++;
        busy_us_ += took_us;
        if (took_us > window_.worst_pass_us) {
            window_.worst_pass_us =
                took_us > 0xFFFFFFFFu ? 0xFFFFFFFFu : static_cast<uint32_t>(took_us);
            window_.worst_pass_phase_ms = pass_phase_ms_;
        }
    }

    diag::Loop take() {
        diag::Loop window = window_;
        const uint64_t busy_ms = busy_us_ / 1000;
        window.busy_ms = busy_ms > 0xFFFF ? 0xFFFF : static_cast<uint16_t>(busy_ms);
        const uint64_t tick_ms = worst_tick_us_ / 1000;
        window.worst_tick_ms = tick_ms > 0xFF ? 0xFF : static_cast<uint8_t>(tick_ms);
        window_ = diag::Loop{};
        busy_us_ = 0;
        worst_tick_us_ = 0;
        return window;
    }

   private:
    const ports::Clock& clock_;
    diag::Loop window_{};
    uint64_t busy_us_{0};
    uint64_t worst_tick_us_{0};
    uint64_t pass_began_us_{0};
    uint16_t pass_phase_ms_{0};
    bool began_{false};
};

}  // namespace skyblip::runtime

#endif
