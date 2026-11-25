#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_MESHTRACKER_X1_POWER_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_MESHTRACKER_X1_POWER_H
#if defined(__ZEPHYR__)

#include <hal/nrf_gpio.h>
#include <hal/nrf_qspi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include "hardware/platform/zephyr/power_down.h"

namespace skyblip::platform::zephyr {

#define SKYBLIP_X1_PIN(node, group, index) pin_of(SKYBLIP_PSEL(node, group, index))

constexpr uint32_t kX1QspiPins[] = {
    SKYBLIP_X1_PIN(DT_NODELABEL(qspi), group1, 0), SKYBLIP_X1_PIN(DT_NODELABEL(qspi), group1, 1),
    SKYBLIP_X1_PIN(DT_NODELABEL(qspi), group1, 2), SKYBLIP_X1_PIN(DT_NODELABEL(qspi), group1, 3),
    SKYBLIP_X1_PIN(DT_NODELABEL(qspi), group1, 4), SKYBLIP_X1_PIN(DT_NODELABEL(qspi), group1, 5)};
constexpr uint32_t kX1I2cPins[] = {SKYBLIP_X1_PIN(DT_ALIAS(sensor_i2c), group1, 0),
                                   SKYBLIP_X1_PIN(DT_ALIAS(sensor_i2c), group1, 1),
                                   SKYBLIP_X1_PIN(DT_ALIAS(rtc_i2c), group1, 0),
                                   SKYBLIP_X1_PIN(DT_ALIAS(rtc_i2c), group1, 1)};
constexpr uint32_t kX1GnssUartPins[] = {SKYBLIP_X1_PIN(DT_ALIAS(gnss_uart), group1, 0),
                                        SKYBLIP_X1_PIN(DT_ALIAS(gnss_uart), group2, 0)};

#undef SKYBLIP_X1_PIN

// INFO: fc 12nov26 core/power's order, walked on the X1's lines:
// boards/seeed/meshtracker_x1/README.md
class MeshtrackerX1Power : public PowerDown {
   public:
    using PowerDown::PowerDown;

   private:
    void perform(power::PowerDownStep step) override {
        switch (step) {
            // INFO: fc 12nov26 the receiver or a refused boot already parked the LR2021
            case power::PowerDownStep::RadioSleep: return;
            // TODO: fc 12nov26 the NOR loses its rail below instead of a DPD, until #169 measures
            // off current
            case power::PowerDownStep::ExternalFlashDeepPowerDown: return;
            case power::PowerDownStep::ExternalFlashLinesReleased: release_flash_lines(); return;
            case power::PowerDownStep::PanelLinesReleased: return;
            case power::PowerDownStep::SensorBusReleased: release_i2c_buses(); return;
            // TODO: fc 12nov26 SoftRF's GNSS enable, kept low through SYSTEM OFF, unverified until
            // #169
            case power::PowerDownStep::GnssStandby: lower(gnss_enable_); return;
            case power::PowerDownStep::GnssLinesReleased: release_gnss_lines(); return;
            // TODO: fc 11nov26 P1.07 and P0.09 as SoftRF and Seeed name them, unverified until #169
            case power::PowerDownStep::PeripheralRailOff:
                lower(sensor_rail_);
                lower(rtc_enable_);
                lower(flash_enable_);
                k_msleep(power::kRailSettleMs);
                return;
            // INFO: fc 12nov26 SENSE is a level, and the X1's button reads high when pressed
            case power::PowerDownStep::WakePinArmed:
                if (button_wakes()) arm_wake(button_);
                return;
        }
    }

    // INFO: fc 12nov26 QSPI owns its six pads while enabled, so it goes before the pads are let go
    static void release_flash_lines() {
        irq_disable(DT_IRQN(DT_NODELABEL(qspi)));
        nrf_qspi_disable(NRF_QSPI);
        for (uint32_t pin : kX1QspiPins) nrf_gpio_cfg_default(pin);
    }

    static void release_i2c_buses() {
        disable_twim(DT_REG_ADDR(DT_ALIAS(sensor_i2c)), DT_IRQN(DT_ALIAS(sensor_i2c)));
        disable_twim(DT_REG_ADDR(DT_ALIAS(rtc_i2c)), DT_IRQN(DT_ALIAS(rtc_i2c)));
        for (uint32_t pin : kX1I2cPins) nrf_gpio_cfg_default(pin);
    }

    void release_gnss_lines() {
        stop_uarte(DT_REG_ADDR(DT_ALIAS(gnss_uart)), DT_IRQN(DT_ALIAS(gnss_uart)));
        for (uint32_t pin : kX1GnssUartPins) nrf_gpio_cfg_default(pin);
        release(gnss_reset_);
        release(gnss_sleep_);
        release(gnss_rtc_wake_);
    }

    struct gpio_dt_spec button_ = GPIO_DT_SPEC_GET(DT_ALIAS(button), gpios);
    struct gpio_dt_spec sensor_rail_ = GPIO_DT_SPEC_GET(DT_ALIAS(sensor_rail), gpios);
    struct gpio_dt_spec flash_enable_ = GPIO_DT_SPEC_GET(DT_ALIAS(flash_enable), gpios);
    struct gpio_dt_spec rtc_enable_ = GPIO_DT_SPEC_GET(DT_ALIAS(rtc_enable), gpios);
    struct gpio_dt_spec gnss_enable_ = GPIO_DT_SPEC_GET(DT_ALIAS(gnss_enable), gpios);
    struct gpio_dt_spec gnss_reset_ = GPIO_DT_SPEC_GET(DT_ALIAS(gnss_reset), gpios);
    struct gpio_dt_spec gnss_sleep_ = GPIO_DT_SPEC_GET(DT_ALIAS(gnss_sleep), gpios);
    struct gpio_dt_spec gnss_rtc_wake_ = GPIO_DT_SPEC_GET(DT_ALIAS(gnss_rtc_wake), gpios);
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
