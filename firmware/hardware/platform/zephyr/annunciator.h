#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_ANNUNCIATOR_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_ANNUNCIATOR_H
#if defined(__ZEPHYR__)

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "core/annunciation/annunciator.h"
#include "ports/annunciator.h"
#include "ports/haptic.h"

namespace skyblip::platform::zephyr {

// A vibration motor wired straight to a pin. This is what the annunciator used
// to be, inlined and named "vibro", and it is the wrong part for the T-Echo
// Plus: P0.08 there is a DRV2605's enable, so driving it high brings a waveform
// driver out of standby and moves nothing. Kept as the platform's default
// haptic, because a pin is always a pin, and because a board that IS fitted with
// a bare motor needs exactly this and nothing more.
class PinMotor : public ports::Haptic {
   public:
    explicit PinMotor(const struct gpio_dt_spec& pin) : pin_(pin) {}

    // Configured on use, not at bring-up. The same pin is a DRV2605's enable on
    // this board, and a begin() that parked it low would switch the waveform
    // driver off after the board had just enabled it.
    void start() override { gpio_pin_configure_dt(&pin_, GPIO_OUTPUT_ACTIVE); }
    void stop() override { gpio_pin_configure_dt(&pin_, GPIO_OUTPUT_INACTIVE); }

   private:
    struct gpio_dt_spec pin_;
};

class Annunciator : public ports::Annunciator {
   public:
    Annunciator(const struct pwm_dt_spec& buzzer, ports::Haptic& haptic)
        : buzzer_(buzzer), haptic_(&haptic) {}

    void begin() {
        tone_guard_.owner = this;
        motor_guard_.owner = this;
        k_work_init_delayable(&tone_guard_.work, cut_tone);
        k_work_init_delayable(&motor_guard_.work, cut_motor);
        silence();
    }

    // INFO: fc 17oct26 schedule, not reschedule: a pitch change must not push the guard back
    void tone(uint16_t pitch_hz, ports::Loudness loudness) override {
        if (pitch_hz == 0) {
            silence();
            return;
        }
        const uint32_t period = PWM_HZ(pitch_hz);
        const uint32_t divisor =
            loudness == ports::Loudness::Loud ? kLoudDutyDivisor : kSoftDutyDivisor;
        pwm_set_dt(&buzzer_, period, period / divisor);
        k_work_schedule(&tone_guard_.work, K_MSEC(kCeilingMs));
    }

    // INFO: fc 17oct26 cancelled in sync, so no cut is still writing the part once an output is off
    void silence() override {
        struct k_work_sync sync;
        k_work_cancel_delayable_sync(&tone_guard_.work, &sync);
        pwm_set_pulse_dt(&buzzer_, 0);
    }

    void vibrate() override {
        haptic_->start();
        k_work_schedule(&motor_guard_.work, K_MSEC(kCeilingMs));
    }

    void still() override {
        struct k_work_sync sync;
        k_work_cancel_delayable_sync(&motor_guard_.work, &sync);
        haptic_->stop();
    }

    void service(uint32_t) {}

    void attach_haptic(ports::Haptic& haptic) { haptic_ = &haptic; }

   private:
    // INFO: fc 15oct26 fundamental goes as sin(pi x duty): half is the most, a tenth is 10 dB under
    static constexpr uint32_t kLoudDutyDivisor = 2;
    static constexpr uint32_t kSoftDutyDivisor = 10;
    static constexpr unsigned kCeilingMs = annunciation::kAnnunciatorOnCeilingMs;

    // INFO: fc 17oct26 a work item, not a k_timer: the DRV2605 stops over I2C, never from an ISR
    struct Guard {
        struct k_work_delayable work;
        Annunciator* owner;
    };

    static void cut_tone(struct k_work* work) {
        printk("annunciator: tone on past %u ms, cut\n", kCeilingMs);
        pwm_set_pulse_dt(&owner_of(work)->buzzer_, 0);
    }

    static void cut_motor(struct k_work* work) {
        printk("annunciator: motor on past %u ms, cut\n", kCeilingMs);
        owner_of(work)->haptic_->stop();
    }

    static Annunciator* owner_of(struct k_work* work) {
        return CONTAINER_OF(k_work_delayable_from_work(work), Guard, work)->owner;
    }

    struct pwm_dt_spec buzzer_;
    ports::Haptic* haptic_;
    Guard tone_guard_{};
    Guard motor_guard_{};
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
