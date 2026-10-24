#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_ANNUNCIATOR_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_ANNUNCIATOR_H

#include <cstdint>

#include "core/annunciation/annunciator.h"
#include "ports/annunciator.h"
#include "ports/haptic.h"

namespace skyblip::platform::host {

// A vibration motor wired straight to a pin: on while it is driven. The virtual
// twin of what the silicon platform does with a GPIO, and what the T-Echo Plus
// turns out NOT to be fitted with.
class PinMotor : public ports::Haptic {
   public:
    void start() override { driving = true; }
    void stop() override { driving = false; }

    bool driving{false};
};

class Annunciator : public ports::Annunciator {
   public:
    void tone(uint16_t pitch_hz, ports::Loudness loudness) override {
        if (pitch_hz == 0) {
            silence();
            return;
        }
        if (!sounding_) tone_guard_.arm();
        pitch_hz_ = pitch_hz;
        loudness_ = loudness;
        sounding_ = true;
        tone_commands_++;
    }

    void silence() override {
        pitch_hz_ = 0;
        sounding_ = false;
        tone_guard_.disarm();
        silences_++;
    }

    void vibrate() override {
        if (!vibrating_) {
            motor_guard_.arm();
            motor_starts_++;
        }
        vibrating_ = true;
        haptic_->start();
    }

    void still() override {
        vibrating_ = false;
        motor_guard_.disarm();
        haptic_->stop();
    }

    // INFO: fc 17oct26 the guards' clock, counted from the first pass after turn-on
    void service(uint32_t now_ms) {
        if (tone_guard_.expired(now_ms)) {
            pitch_hz_ = 0;
            sounding_ = false;
            guard_cuts_++;
        }
        if (motor_guard_.expired(now_ms)) {
            vibrating_ = false;
            haptic_->stop();
            guard_cuts_++;
        }
    }

    void attach_haptic(ports::Haptic& haptic) { haptic_ = &haptic; }

    uint16_t pitch_hz() const { return pitch_hz_; }
    bool sounding() const { return sounding_; }
    ports::Loudness loudness() const { return loudness_; }
    bool vibrating() const { return vibrating_; }

    uint32_t tone_commands() const { return tone_commands_; }
    uint32_t silences() const { return silences_; }
    uint32_t motor_starts() const { return motor_starts_; }
    uint32_t guard_cuts() const { return guard_cuts_; }
    PinMotor& pin_motor() { return pin_motor_; }

   private:
    struct Guard {
        bool armed{false};
        bool started{false};
        uint32_t start_ms{0};

        void arm() {
            armed = true;
            started = false;
        }
        void disarm() { armed = false; }
        bool expired(uint32_t now_ms) {
            if (!armed) return false;
            if (!started) {
                started = true;
                start_ms = now_ms;
            }
            if (now_ms - start_ms < annunciation::kAnnunciatorOnCeilingMs) return false;
            armed = false;
            return true;
        }
    };

    uint16_t pitch_hz_{0};
    ports::Loudness loudness_{ports::Loudness::Loud};
    bool sounding_{false};
    bool vibrating_{false};
    uint32_t tone_commands_{0}, silences_{0}, motor_starts_{0}, guard_cuts_{0};
    PinMotor pin_motor_{};
    ports::Haptic* haptic_{&pin_motor_};
    Guard tone_guard_{};
    Guard motor_guard_{};
};

}  // namespace skyblip::platform::host

#endif
