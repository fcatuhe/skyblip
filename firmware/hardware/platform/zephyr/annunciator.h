#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_ANNUNCIATOR_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_ANNUNCIATOR_H
#if defined(__ZEPHYR__)

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>

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
        haptic_end_.owner = this;
        k_work_init_delayable(&haptic_end_.work, haptic_off);
        silence();
    }

    void tone(uint16_t hz, ports::Loudness loudness) override {
        if (hz == 0) return silence();
        const uint32_t period = PWM_HZ(hz);
        const uint32_t divisor =
            loudness == ports::Loudness::Loud ? kLoudDutyDivisor : kSoftDutyDivisor;
        pwm_set_dt(&buzzer_, period, period / divisor);
    }

    // The pulse and the timer that ends it, per ports/annunciator.h. What the pulse
    // reaches - a pin, or a DRV2605 over I2C - is the board's business: it
    // attaches whichever it found.
    // A delayed work item and not a k_timer: a timer's callback runs in interrupt
    // context, and the DRV2605 is stopped over I2C, which may not be called from
    // there. Rescheduled rather than started, so a second pulse that lands inside
    // the first extends it instead of being cut short by the first one's deadline.
    void vibrate(uint16_t ms) override {
        haptic_->start();
        k_work_reschedule(&haptic_end_.work, K_MSEC(ms));
    }

    void silence() override { pwm_set_pulse_dt(&buzzer_, 0); }

    // The one pass of the loop the host platform needs to end a pulse; here the
    // kernel timer has already done it.
    void service(uint32_t) {}

    void attach_haptic(ports::Haptic& haptic) { haptic_ = &haptic; }

   private:
    // INFO: fc 15oct26 fundamental goes as sin(pi x duty): half is the most, a tenth is 10 dB under
    static constexpr uint32_t kLoudDutyDivisor = 2;
    static constexpr uint32_t kSoftDutyDivisor = 10;

    // A plain struct, so CONTAINER_OF is arithmetic on a standard-layout type
    // rather than an offsetof into a class with a vtable.
    struct PulseEnd {
        struct k_work_delayable work;
        Annunciator* owner;
    };

    static void haptic_off(struct k_work* work) {
        PulseEnd* end = CONTAINER_OF(k_work_delayable_from_work(work), PulseEnd, work);
        end->owner->haptic_->stop();
    }

    struct pwm_dt_spec buzzer_;
    ports::Haptic* haptic_;
    PulseEnd haptic_end_{};
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
