#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_POWER_DOWN_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_POWER_DOWN_H
#if defined(__ZEPHYR__)

#include <hal/nrf_gpio.h>
#include <hal/nrf_twim.h>
#include <hal/nrf_uarte.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/dt-bindings/pinctrl/nrf-pinctrl.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/poweroff.h>

#include "core/power/shutdown.h"
#include "hardware/platform/zephyr/dfu.h"
#include "hardware/platform/zephyr/reset_causes.h"
#include "hardware/platform/zephyr/supply_monitor.h"
#include "ports/system_power.h"

namespace skyblip::platform::zephyr {

#define SKYBLIP_PSEL(node, group, index) \
    DT_PROP_BY_IDX(DT_CHILD(DT_PINCTRL_BY_NAME(node, default, 0), group), psels, index)

constexpr uint32_t pin_of(uint32_t psel) { return (psel >> NRF_PIN_POS) & NRF_PIN_MSK; }
constexpr uint32_t function_of(uint32_t psel) { return (psel >> NRF_FUN_POS) & NRF_FUN_MSK; }

// INFO: fc 08nov26 ten 8N1 characters at 4800 baud, the slowest rate either GNSS UART runs at
constexpr uint32_t kUarteStopTimeoutUs = 21000;
constexpr uint32_t kUarteStopPollUs = 10;

// INFO: fc 19nov26 every board's way into SYSTEM OFF; its perform() walks core/power's order
class PowerDown : public ports::SystemPower, protected power::PowerDownSink {
   public:
    // INFO: fc 12nov26 read once and cleared: a second read of RESETREAS reads as a power-on
    void read_causes() {
        if (causes_read_) return;
        causes_read_ = true;
        causes_ = read_reset_causes();
        hwinfo_clear_reset_cause();
    }

    void arm_supply_monitor() { supply_monitor_.arm(); }

    power::ResetCause reset_causes() const override { return causes_; }
    bool supply_monitor_armed() const override { return supply_monitor_.armed(); }
    bool take_supply_warning() override { return supply_monitor_.take_warning(); }

   protected:
    void power_off(power::DfuButton factory_dfu_button) {
        // INFO: fc 05oct26 first, as it outlives the rails; no retention area, no recovery either
        (void)write_boot_magic(Dfu::boot_magic_for_system_off(factory_dfu_button));
        power::power_down(*this);
        // INFO: fc 05aug26 after the walk, which arms a level wake: a stale DETECT wakes us at once
        clear_stale_latches();
        sys_poweroff();
    }

    static void lower(const struct gpio_dt_spec& pin) {
        (void)gpio_pin_configure_dt(&pin, GPIO_OUTPUT_INACTIVE);
    }

    // INFO: fc 08nov26 GPIO_DISCONNECTED resets only a pin nrfx holds (pin_uninit), so input first
    static void release(const struct gpio_dt_spec& pin) {
        (void)gpio_pin_configure_dt(&pin, GPIO_INPUT);
        (void)gpio_pin_configure_dt(&pin, GPIO_DISCONNECTED);
    }

    // INFO: fc 08nov26 Zephyr leaves a TWIM enabled without device PM (i2c_nrfx_twim_common.c)
    static void disable_twim(uintptr_t address, unsigned int irq) {
        irq_disable(irq);
        nrf_twim_disable(reinterpret_cast<NRF_TWIM_Type*>(address));
    }

    // INFO: fc 19nov26 nRF52840 PS 6.34.7, as uart_nrfx_uarte.c suspends: RXTO and TXSTOPPED first
    static void stop_uarte(uintptr_t address, unsigned int irq) {
        NRF_UARTE_Type* uarte = reinterpret_cast<NRF_UARTE_Type*>(address);
        irq_disable(irq);
        nrf_uarte_shorts_disable(uarte,
                                 NRF_UARTE_SHORT_ENDRX_STARTRX | NRF_UARTE_SHORT_ENDRX_STOPRX);
        nrf_uarte_event_clear(uarte, NRF_UARTE_EVENT_RXTO);
        nrf_uarte_event_clear(uarte, NRF_UARTE_EVENT_TXSTOPPED);
        nrf_uarte_task_trigger(uarte, NRF_UARTE_TASK_STOPRX);
        nrf_uarte_task_trigger(uarte, NRF_UARTE_TASK_STOPTX);
        for (uint32_t waited_us = 0; waited_us < kUarteStopTimeoutUs;
             waited_us += kUarteStopPollUs) {
            if (nrf_uarte_event_check(uarte, NRF_UARTE_EVENT_RXTO) &&
                nrf_uarte_event_check(uarte, NRF_UARTE_EVENT_TXSTOPPED))
                break;
            k_busy_wait(kUarteStopPollUs);
        }
        nrf_uarte_disable(uarte);
    }

    // INFO: hk 02aug26 SYSTEM OFF wakes on GPIO SENSE, a level, so the button is released by now
    static void arm_wake(const struct gpio_dt_spec& button) {
        (void)gpio_pin_configure_dt(&button, GPIO_INPUT);
        (void)gpio_pin_interrupt_configure_dt(&button, GPIO_INT_LEVEL_ACTIVE);
    }

   private:
    SupplyMonitor supply_monitor_{};
    power::ResetCause causes_{power::ResetCause::None};
    bool causes_read_{false};
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
