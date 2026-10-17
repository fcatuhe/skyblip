#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_ANNUNCIATOR_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_ANNUNCIATOR_H

#include <cstdint>

#include "ports/annunciator.h"
#include "ports/haptic.h"

namespace skyblip::platform::host {

// A vibration motor wired straight to a pin: on while it is driven. The virtual
// twin of what the silicon platform can do with a GPIO and a timer, and what the
// T-Echo Plus turns out NOT to be fitted with.
class PinMotor : public ports::Haptic {
   public:
    void start() override { driving = true; }
    void stop() override { driving = false; }

    bool driving{false};
};

class Annunciator : public ports::Annunciator {
   public:
    void tone(uint16_t hz, ports::Loudness loudness) override {
        hz_ = hz;
        loudness_ = loudness;
        sounding_ = hz != 0;
        tone_commands_++;
    }

    // The pulse and its end, both here: whatever the haptic is, the length of the
    // pulse is the adapter's to own (ports/annunciator.h). The host has no timer,
    // so the end is the caller's next tick asking for the time - which is what
    // service() is, driven by the board's poll.
    void vibrate(uint16_t ms) override {
        haptic_ms_ = ms;
        haptic_pulses_++;
        haptic_->start();
        pulse_ends_ms_ = ms;
        pulsing_ = true;
        started_ = false;
    }

    void silence() override {
        hz_ = 0;
        sounding_ = false;
        silences_++;
    }

    // Ends a pulse whose time is up. The silicon platform has a kernel timer for
    // this; the host has the loop, so the board calls it once per pass. The
    // deadline is a duration from the first pass after the pulse started, so a
    // test that never advances the clock leaves the motor running, exactly as the
    // hardware would.
    void service(uint32_t now_ms) {
        if (!pulsing_) return;
        if (!started_) {
            started_ = true;
            start_ms_ = now_ms;
        }
        if (now_ms - start_ms_ < pulse_ends_ms_) return;
        haptic_->stop();
        pulsing_ = false;
        started_ = false;
    }

    void attach_haptic(ports::Haptic& haptic) { haptic_ = &haptic; }

    uint16_t hz() const { return hz_; }
    bool sounding() const { return sounding_; }
    ports::Loudness loudness() const { return loudness_; }
    uint16_t haptic_ms() const { return haptic_ms_; }

    uint32_t tone_commands() const { return tone_commands_; }
    uint32_t silences() const { return silences_; }
    uint32_t haptic_pulses() const { return haptic_pulses_; }
    PinMotor& pin_motor() { return pin_motor_; }

   private:
    uint16_t hz_{0};
    ports::Loudness loudness_{ports::Loudness::Loud};
    bool sounding_{false};
    uint16_t haptic_ms_{0};
    uint32_t tone_commands_{0}, silences_{0}, haptic_pulses_{0};
    PinMotor pin_motor_{};
    ports::Haptic* haptic_{&pin_motor_};
    uint32_t start_ms_{0};
    uint32_t pulse_ends_ms_{0};
    bool pulsing_{false};
    bool started_{false};
};

}  // namespace skyblip::platform::host

#endif
